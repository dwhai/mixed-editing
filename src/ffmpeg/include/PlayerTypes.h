//
// Created by Anlk on 2026/6/3.
// 播放器公共数据类型：解码帧 + 线程安全队列
//

#ifndef MIXEDEDITING_PLAYERTYPES_H
#define MIXEDEDITING_PLAYERTYPES_H

#include <QByteArray>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace Mixed::Player {

    // 一帧已解码、已转换为 YUV420P（三平面紧凑排列）的视频帧。
    // pts 单位为秒，用于音画同步。
    struct VideoFrame {
        QByteArray y;          // 亮度平面，大小 = width * height
        QByteArray u;          // 色度 U 平面，大小 = (width/2) * (height/2)
        QByteArray v;          // 色度 V 平面，大小 = (width/2) * (height/2)
        int width = 0;
        int height = 0;
        double pts = 0.0;      // 显示时间戳（秒）
        bool valid() const { return width > 0 && height > 0 && !y.isEmpty(); }
    };

    // 一段已解码、已重采样为 S16 交错格式的音频数据。
    struct AudioFrame {
        QByteArray pcm;        // 交错的 16bit PCM 数据
        double pts = 0.0;      // 该段音频起始时间戳（秒）
    };

    // 简单的有界线程安全队列：生产者满则等待，消费者空则等待。
    // 通过 abort() 唤醒所有等待线程以便安全退出。
    template <typename T>
    class ThreadSafeQueue {
    public:
        explicit ThreadSafeQueue(size_t maxSize = 0) : m_maxSize(maxSize) {}

        // 入队（队满则阻塞）。返回 false 表示被 abort 唤醒。
        bool push(T value) {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_maxSize > 0) {
                m_notFull.wait(lock, [this] {
                    return m_aborted || m_queue.size() < m_maxSize;
                });
            }
            if (m_aborted) return false;
            m_queue.push_back(std::move(value));
            m_notEmpty.notify_one();
            return true;
        }

        // 出队（队空则阻塞）。返回 false 表示被 abort 唤醒。
        bool pop(T &out) {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_notEmpty.wait(lock, [this] {
                return m_aborted || !m_queue.empty();
            });
            if (m_aborted && m_queue.empty()) return false;
            out = std::move(m_queue.front());
            m_queue.pop_front();
            m_notFull.notify_one();
            return true;
        }

        // 非阻塞查看队首（不出队）。
        bool peek(T &out) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_queue.empty()) return false;
            out = m_queue.front();
            return true;
        }

        // 非阻塞出队。
        bool tryPop(T &out) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_queue.empty()) return false;
            out = std::move(m_queue.front());
            m_queue.pop_front();
            m_notFull.notify_one();
            return true;
        }

        size_t size() {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.size();
        }

        bool empty() {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.empty();
        }

        // 唤醒所有等待者并进入中止状态（用于线程退出）。
        void abort() {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_aborted = true;
            m_notEmpty.notify_all();
            m_notFull.notify_all();
        }

        // 清空队列并复位中止标记，准备下一次播放。
        void reset() {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.clear();
            m_aborted = false;
        }

        bool aborted() {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_aborted;
        }

    private:
        std::deque<T> m_queue;
        size_t m_maxSize;
        std::mutex m_mutex;
        std::condition_variable m_notEmpty;
        std::condition_variable m_notFull;
        bool m_aborted = false;
    };

} // namespace Mixed::Player

#endif //MIXEDEDITING_PLAYERTYPES_H
