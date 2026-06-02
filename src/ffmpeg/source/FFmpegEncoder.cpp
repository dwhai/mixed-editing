//
// Created by Anlk on 2026/6/3.
//

#include "../include/FFmpegEncoder.h"
#include <iostream>

using namespace std;

// ============================================
// FFmpegEncoder 编码器实现
// ============================================

FFmpegEncoder::FFmpegEncoder() {
    cout << "FFmpegEncoder::FFmpegEncoder() - 创建编码器" << endl;
}

FFmpegEncoder::~FFmpegEncoder() {
    close();
    cout << "FFmpegEncoder::~FFmpegEncoder() - 销毁编码器" << endl;
}

bool FFmpegEncoder::initVideoEncoder(int width, int height, int fps, const std::string& codecName) {
    // 查找编码器
    codec = avcodec_find_encoder_by_name(codecName.c_str());
    if (!codec) {
        cerr << "Error: 找不到编码器 " << codecName << endl;
        return false;
    }

    // 分配编码器上下文
    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        cerr << "Error: 分配编码器上下文失败" << endl;
        return false;
    }

    // 设置编码器参数
    codecCtx->width = width;
    codecCtx->height = height;
    codecCtx->time_base = {1, fps};
    codecCtx->framerate = {fps, 1};
    codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    codecCtx->bit_rate = 500000; // 500kbps
    codecCtx->gop_size = 10;
    codecCtx->max_b_frames = 1;

    // 设置 H.264 特定选项
    if (codec->type == AVMEDIA_TYPE_VIDEO && codec->id == AV_CODEC_ID_H264) {
        av_opt_set(codecCtx->priv_data, "preset", "medium", 0);
        av_opt_set(codecCtx->priv_data, "tune", "zerolatency", 0);
    }

    // 打开编码器
    int ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        cerr << "Error: 打开编码器失败" << endl;
        avcodec_free_context(&codecCtx);
        return false;
    }

    cout << "Info: 编码器初始化成功 - " << width << "x" << height << "@" << fps << "fps" << endl;
    return true;
}

int FFmpegEncoder::encodeFrame(AVFrame* frame, AVPacket* pkt) {
    int ret = avcodec_send_frame(codecCtx, frame);
    if (ret < 0) {
        cerr << "Error: 发送帧到编码器失败" << endl;
        return ret;
    }

    ret = avcodec_receive_packet(codecCtx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;
    } else if (ret < 0) {
        cerr << "Error: 从编码器接收数据包失败" << endl;
        return ret;
    }

    return 1;
}

void FFmpegEncoder::close() {
    if (codecCtx) {
        // 刷新编码器
        AVPacket* pkt = av_packet_alloc();
        avcodec_send_frame(codecCtx, nullptr);
        while (avcodec_receive_packet(codecCtx, pkt) >= 0) {
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
        
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
    }
}

// ============================================
// FFmpegDecoder 解码器实现
// ============================================

FFmpegDecoder::FFmpegDecoder() {
    cout << "FFmpegDecoder::FFmpegDecoder() - 创建解码器" << endl;
}

FFmpegDecoder::~FFmpegDecoder() {
    close();
    cout << "FFmpegDecoder::~FFmpegDecoder() - 销毁解码器" << endl;
}

bool FFmpegDecoder::openFile(const std::string& filename) {
    // 打开输入文件
    int ret = avformat_open_input(&fmtCtx, filename.c_str(), nullptr, nullptr);
    if (ret < 0) {
        cerr << "Error: 无法打开文件 " << filename << endl;
        return false;
    }

    // 读取文件流信息
    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        cerr << "Error: 找不到流信息" << endl;
        avformat_close_input(&fmtCtx);
        return false;
    }

    cout << "Info: 成功打开文件 - " << filename << endl;
    return true;
}

bool FFmpegDecoder::initDecoder() {
    if (!fmtCtx) {
        cerr << "Error: 格式上下文未初始化" << endl;
        return false;
    }

    // 查找视频流
    videoStreamIndex = -1;
    for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            videoStream = fmtCtx->streams[i];
            break;
        }
    }

    if (videoStreamIndex == -1) {
        cerr << "Error: 找不到视频流" << endl;
        return false;
    }

    // 查找解码器
    const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        cerr << "Error: 找不到视频解码器" << endl;
        return false;
    }

    // 分配解码器上下文
    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        cerr << "Error: 分配解码器上下文失败" << endl;
        return false;
    }

    // 复制参数
    avcodec_parameters_to_context(codecCtx, videoStream->codecpar);

    // 打开解码器
    int ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        cerr << "Error: 打开解码器失败" << endl;
        avcodec_free_context(&codecCtx);
        return false;
    }

    // 获取视频信息
    width = codecCtx->width;
    height = codecCtx->height;
    AVRational framerate = av_guess_frame_rate(fmtCtx, videoStream, nullptr);
    fps = av_q2d(framerate);

    cout << "Info: 解码器初始化成功 - " << width << "x" << height << "@" << fps << "fps" << endl;
    return true;
}

int FFmpegDecoder::decodeFrame(AVFrame* frame, AVPacket* pkt) {
    while (true) {
        int ret = av_read_frame(fmtCtx, pkt);
        if (ret == AVERROR_EOF) {
            return 0; // 文件结束
        } else if (ret < 0) {
            cerr << "Error: 读取帧失败" << endl;
            return ret;
        }

        // 只处理视频流
        if (pkt->stream_index != videoStreamIndex) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(codecCtx, pkt);
        if (ret < 0) {
            cerr << "Error: 发送数据包到解码器失败" << endl;
            av_packet_unref(pkt);
            return ret;
        }

        ret = avcodec_receive_frame(codecCtx, frame);
        av_packet_unref(pkt);

        if (ret == AVERROR(EAGAIN)) {
            continue; // 需要更多数据
        } else if (ret == AVERROR_EOF) {
            return 0;
        } else if (ret < 0) {
            cerr << "Error: 从解码器接收帧失败" << endl;
            return ret;
        }

        return 1; // 成功解码一帧
    }
}

void FFmpegDecoder::close() {
    if (codecCtx) {
        avcodec_free_context(&codecCtx);
        codecCtx = nullptr;
    }
    if (fmtCtx) {
        avformat_close_input(&fmtCtx);
        fmtCtx = nullptr;
    }
    videoStream = nullptr;
    videoStreamIndex = -1;
}

// ============================================
// FFmpegUtils 工具函数实现
// ============================================

namespace FFmpegUtils {

AVFrame* createFrame(int width, int height, AVPixelFormat format) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        return nullptr;
    }

    frame->format = format;
    frame->width = width;
    frame->height = height;

    int ret = av_frame_get_buffer(frame, 32);
    if (ret < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    return frame;
}

void freeFrame(AVFrame* frame) {
    if (frame) {
        av_frame_free(&frame);
    }
}

int convertImage(const uint8_t* srcData[], const int srcLinesize[],
                uint8_t* dstData[], const int dstLinesize[],
                int width, int height,
                AVPixelFormat srcFormat, AVPixelFormat dstFormat) {
    SwsContext* swsCtx = sws_getContext(
        width, height, srcFormat,
        width, height, dstFormat,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsCtx) {
        return -1;
    }

    sws_scale(swsCtx, srcData, srcLinesize, 0, height, dstData, dstLinesize);
    sws_freeContext(swsCtx);

    return 0;
}

} // namespace FFmpegUtils
