//
// Created by Anlk on 2026/6/3.
//

#ifndef MIXEDEDITING_ENCODE_H
#define MIXEDEDITING_ENCODE_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <string>

class FFmpegEncoder {
public:
    FFmpegEncoder();
    ~FFmpegEncoder();

    // 初始化视频编码器
    bool initVideoEncoder(int width, int height, int fps, const std::string& codecName = "libx264");
    
    // 编码视频帧
    int encodeFrame(AVFrame* frame, AVPacket* pkt);
    
    // 获取编码器上下文
    AVCodecContext* getCodecContext() const { return codecCtx; }
    
    // 关闭编码器
    void close();

private:
    AVCodecContext* codecCtx = nullptr;
    const AVCodec* codec = nullptr;
};

class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    // 打开视频文件
    bool openFile(const std::string& filename);
    
    // 初始化解码器
    bool initDecoder();
    
    // 读取并解码一帧
    int decodeFrame(AVFrame* frame, AVPacket* pkt);
    
    // 获取视频信息
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    double getFPS() const { return fps; }
    
    // 获取解码器上下文
    AVCodecContext* getCodecContext() const { return codecCtx; }
    AVFormatContext* getFormatContext() const { return fmtCtx; }
    
    // 关闭解码器
    void close();

private:
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVStream* videoStream = nullptr;
    int videoStreamIndex = -1;
    int width = 0;
    int height = 0;
    double fps = 0.0;
};

// 工具函数
namespace FFmpegUtils {
    // 创建指定格式的视频帧
    AVFrame* createFrame(int width, int height, AVPixelFormat format = AV_PIX_FMT_YUV420P);
    
    // 释放视频帧
    void freeFrame(AVFrame* frame);
    
    // 图像格式转换（RGB -> YUV）
    int convertImage(const uint8_t* srcData[], const int srcLinesize[],
                    uint8_t* dstData[], const int dstLinesize[],
                    int width, int height,
                    AVPixelFormat srcFormat, AVPixelFormat dstFormat);
}

#endif //MIXEDEDITING_ENCODE_H