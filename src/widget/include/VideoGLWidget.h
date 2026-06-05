//
// Created by Anlk on 2026/6/3.
// 视频显示控件：基于 QOpenGLWidget，使用 GLSL 着色器在 GPU 上完成
// YUV420P -> RGB 的颜色空间转换，实现硬件加速渲染。
//

#ifndef MIXEDEDITING_VIDEOGLWIDGET_H
#define MIXEDEDITING_VIDEOGLWIDGET_H

#include "../../ffmpeg/include/PlayerTypes.h"

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <mutex>

namespace Mixed::Player {

    class VideoGLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
        Q_OBJECT

    public:
        explicit VideoGLWidget(QWidget *parent = nullptr);
        ~VideoGLWidget() override;

        // 由 GUI 线程调用：提交一帧待显示的画面并请求重绘。
        void setFrame(const VideoFrame &frame);

        // 清空画面（停止播放时调用）。
        void clearFrame();

    protected:
        void initializeGL() override;
        void paintGL() override;
        void resizeGL(int w, int h) override;

    private:
        void uploadTextures();
        void ensureTextures();

        QOpenGLShaderProgram *m_program = nullptr;
        GLuint m_textures[3] = {0, 0, 0};
        int m_texWidth = 0;
        int m_texHeight = 0;

        VideoFrame m_pending;        // 待上传纹理的帧
        bool m_hasPending = false;
        bool m_hasFrame = false;
        std::mutex m_mutex;

        int m_vertexAttr = -1;
        int m_texCoordAttr = -1;
    };

} // namespace Mixed::Player

#endif //MIXEDEDITING_VIDEOGLWIDGET_H
