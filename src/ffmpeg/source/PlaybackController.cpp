//
// Created by Anlk on 2026/6/7.
// PlaybackController 实现。
//

#include "../include/PlaybackController.h"
#include "../include/CompositionEngine.h"
#include "../include/AudioOutput.h"

#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace Mixed::Player {

    PlaybackController::PlaybackController(QObject *parent) : QObject(parent) {
        m_audio = new AudioOutput(this);
        m_tick = new QTimer(this);
        m_tick->setTimerType(Qt::PreciseTimer);
        m_tick->setInterval(5);
        connect(m_tick, &QTimer::timeout, this, &PlaybackController::onTick);
    }

    PlaybackController::~PlaybackController() {
        stop();
    }

    void PlaybackController::setCanvasSize(int width, int height) {
        if (width > 0 && height > 0) {
            m_canvasW = width;
            m_canvasH = height;
        }
    }

    void PlaybackController::setTimeline(
        const std::vector<DB::Track> &tracks,
        const QHash<QString, std::vector<DB::Clip>> &clipsByTrack,
        const QHash<QString, QString> &assetPathByClip,
        qint64 durationUs, int fps) {
        // 播放中不允许直接换轨：调用方（EditorWindow）会在 rebuild 时先 stop。
        m_tracks = tracks;
        m_durationUs = durationUs;
        m_fps = fps > 0 ? fps : 30;

        m_clipsByTrack.clear();
        for (auto it = clipsByTrack.constBegin(); it != clipsByTrack.constEnd(); ++it) {
            m_clipsByTrack.emplace(it.key(), it.value());
        }
        m_pathMap.clear();
        for (auto it = assetPathByClip.constBegin(); it != assetPathByClip.constEnd(); ++it) {
            m_pathMap.emplace(it.key(), it.value());
        }

        // 音频混流快照（QHash 直接复用）。
        m_audioSrc.tracks = tracks;
        m_audioSrc.clipsByTrack = clipsByTrack;
        m_audioSrc.assetPathByClip = assetPathByClip;

        m_hasAudio = false;
        for (const DB::Track &t : tracks) {
            if (t.trackType == QStringLiteral("audio")) {
                auto cit = clipsByTrack.constFind(t.id);
                if (cit != clipsByTrack.constEnd() && !cit.value().empty()) {
                    m_hasAudio = true;
                    break;
                }
            }
        }
    }

    void PlaybackController::setVolume(qreal volume) {
        m_volume = volume;
        if (m_audio) m_audio->setVolume(volume);
    }

    void PlaybackController::play(qint64 startUs) {
        if (m_durationUs <= 0) {
            return;
        }
        stopWorkers();   // 清掉任何残留
        if (startUs < 0) startUs = 0;
        if (startUs >= m_durationUs) startUs = 0;   // 从头播
        startWorkers(startUs);
        m_playing = true;
        m_paused = false;
    }

    void PlaybackController::pause() {
        if (!m_playing || m_paused) return;
        // 捕获当前主时钟到锚点，使恢复时从此处继续（静音兜底路径不会自更新锚点）。
        m_clockAnchorPts = masterSeconds();
        m_paused = true;
        m_tick->stop();
        if (m_audio) m_audio->pause();
    }

    void PlaybackController::resume() {
        if (!m_playing || !m_paused) return;
        m_paused = false;
        // 恢复时重锚时钟：从暂停处（m_clockAnchorPts）继续，墙钟从 0 重新计。
        m_lastAudioClock = -1.0;
        m_anchorTimer.restart();
        if (m_audio) m_audio->resume();
        m_tick->start();
    }

    void PlaybackController::seek(qint64 us) {
        const bool wasPlaying = m_playing && !m_paused;
        stopWorkers();
        if (wasPlaying) {
            play(us);
        }
    }

    void PlaybackController::stop() {
        stopWorkers();
        m_playing = false;
        m_paused = false;
    }

    void PlaybackController::startWorkers(qint64 startUs) {
        m_abort.store(false);
        m_videoDone.store(false);
        m_prerolled = false;
        m_startUs = startUs;
        m_frameQueue.reset();

        // 声卡：48k 立体声（与 AudioMix 一致），便于主时钟直接映射时间线。
        if (m_hasAudio) {
            m_audio->start(kAudioRate, kAudioCh);
            m_audio->setVolume(m_volume);
            m_audio->clear();
        }

        // 时钟锚点初始化为起点。
        m_clockAnchorPts = startUs / 1'000'000.0;
        m_lastAudioClock = -1.0;
        m_anchorTimer.restart();

        const qint64 startFrame = static_cast<qint64>(startUs / 1'000'000.0 * m_fps);
        m_videoThread = std::thread([this, startFrame] { videoThreadMain(startFrame); });
        if (m_hasAudio) {
            m_audioThread = std::thread([this, startUs] { audioThreadMain(startUs); });
        }
        m_tick->start();
    }

    void PlaybackController::stopWorkers() {
        m_tick->stop();
        m_abort.store(true);
        m_frameQueue.abort();      // 唤醒视频线程的阻塞 push
        if (m_videoThread.joinable()) m_videoThread.join();
        if (m_audioThread.joinable()) m_audioThread.join();
        if (m_audio && m_audio->isRunning()) {
            m_audio->stop();
        }
        m_frameQueue.reset();      // 复位 abort 标志并清空，供下次播放
    }

    void PlaybackController::videoThreadMain(qint64 startFrame) {
        // 本线程独占的合成引擎（不与预览/导出共享）。
        CompositionEngine engine;
        engine.setCanvasSize(m_canvasW, m_canvasH);
        engine.setTimeline(m_tracks, m_clipsByTrack, m_pathMap);

        const qint64 totalFrames =
            std::max<qint64>(1, static_cast<qint64>(std::ceil(m_durationUs / 1'000'000.0 * m_fps)));

        for (qint64 i = startFrame; i < totalFrames; ++i) {
            if (m_abort.load()) break;
            const qint64 tUs = static_cast<qint64>(i * 1'000'000.0 / m_fps);
            VideoFrame f = engine.composeAt(tUs);
            f.pts = tUs / 1'000'000.0;   // 绝对时间线秒
            if (!m_frameQueue.push(std::move(f))) {
                break;   // 被 abort
            }
        }
        m_videoDone.store(true);
    }

    void PlaybackController::audioThreadMain(qint64 startUs) {
        const qint64 startSample =
            static_cast<qint64>(std::llround(startUs / 1'000'000.0 * kAudioRate));
        const qint64 totalSamples =
            static_cast<qint64>(std::ceil(m_durationUs / 1'000'000.0 * kAudioRate));
        if (startSample >= totalSamples) return;

        // 一次性混完整段（单一解码器/重采样器，无逐块重 seek 与 SwrContext 重建），
        // 从根上消除每块边界的相位断点（杂音）。代价是起播前的一次混音延迟，
        // 与导出走同一套 mixAudioRange 逻辑，音质一致。
        std::vector<float> mixed;
        mixAudioRange(m_audioSrc, startSample, totalSamples, mixed, m_abort);
        if (m_abort.load()) return;

        // 软限幅（与导出一致）。
        for (float &v : mixed) v = std::clamp(v, -1.0f, 1.0f);

        // 按固定切片（~50ms）流式喂给声卡，PTS 为绝对时间线秒。
        const qint64 sliceSamples = kAudioRate / 20;     // 50ms
        const qint64 totalFrames = static_cast<qint64>(mixed.size()) / kAudioCh;
        qint64 pos = 0;
        while (!m_abort.load() && pos < totalFrames) {
            const qint64 n = std::min(sliceSamples, totalFrames - pos);

            AudioFrame af;
            af.pts = static_cast<double>(startSample + pos) / kAudioRate;
            af.pcm.resize(static_cast<int>(n) * kAudioCh * 2);   // S16 交错
            auto *out = reinterpret_cast<int16_t *>(af.pcm.data());
            for (qint64 i = 0; i < n * kAudioCh; ++i) {
                out[i] = static_cast<int16_t>(std::lround(mixed[static_cast<size_t>(pos) * kAudioCh + i] * 32767.0f));
            }
            m_audio->enqueue(af);
            pos += n;

            // 缓冲节流：声卡已缓冲 >0.6s 则等一等，避免内存堆积。
            while (!m_abort.load() && m_audio->bufferedSeconds() > 0.6) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }

    double PlaybackController::masterSeconds() {
        // 音频时钟有效（已越过起点且在推进）→ 锚定到音频；否则用墙钟从锚点平滑推进。
        // 注意：声卡未真正起播时 masterClock() 返回 0，对 startUs>0 的播放不可采信，
        // 故要求 a 至少达到起点附近才接受，避免播放头被错误拉回 0。
        const double startSec = m_startUs / 1'000'000.0;
        if (m_hasAudio) {
            const double a = m_audio->masterClock();
            if (a >= startSec - 1e-3 && a > m_lastAudioClock + 1e-6) {
                m_lastAudioClock = a;
                m_clockAnchorPts = a;
                m_anchorTimer.restart();
                return a;
            }
        }
        return m_clockAnchorPts + m_anchorTimer.elapsed() / 1000.0;
    }

    void PlaybackController::onTick() {
        if (!m_playing || m_paused) return;

        // 预滚：先攒够帧再出，避免起播瞬间空队列。
        if (!m_prerolled) {
            if (m_frameQueue.size() >= 2 || m_videoDone.load()) {
                m_prerolled = true;
            } else {
                return;
            }
        }

        const double master = masterSeconds();

        // 丢弃所有 pts <= master 的帧，保留最后一个作为当前帧（追上主时钟）。
        VideoFrame chosen;
        bool haveChosen = false;
        VideoFrame head;
        while (m_frameQueue.peek(head)) {
            if (head.pts <= master + 1e-3) {
                VideoFrame popped;
                if (!m_frameQueue.tryPop(popped)) break;
                chosen = std::move(popped);
                haveChosen = true;
            } else {
                break;
            }
        }

        if (haveChosen && chosen.valid()) {
            emit frameReady(chosen);
            emit positionChanged(static_cast<qint64>(chosen.pts * 1'000'000.0));
        } else {
            // 没有到点的新帧：仍按主时钟推进播放头（画面维持上一帧）。
            emit positionChanged(static_cast<qint64>(master * 1'000'000.0));
        }

        // 结束判定：视频已全部产出且队列取空。
        if (m_videoDone.load() && m_frameQueue.empty()) {
            const double endSec = m_durationUs / 1'000'000.0;
            if (master >= endSec - 1e-3 || !haveChosen) {
                m_tick->stop();
                m_playing = false;
                m_paused = false;
                emit reachedEnd();
            }
        }
    }

} // namespace Mixed::Player
