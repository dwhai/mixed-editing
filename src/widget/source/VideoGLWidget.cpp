//
// Created by Anlk on 2026/6/3.
// VideoGLWidget 实现。
//

#include "../include/VideoGLWidget.h"

namespace Mixed::Player {

    namespace {
        // 顶点着色器：直接传递裁剪空间坐标与纹理坐标（GLSL 1.20，兼容 macOS 2.1 上下文）。
        const char *kVertexShader = R"(
attribute vec2 aPos;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

        // 片段着色器：在 GPU 上完成 YUV(BT.601) -> RGB 转换。
        const char *kFragmentShader = R"(
#ifdef GL_ES
precision mediump float;
#endif
uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;
varying vec2 vTexCoord;
void main() {
    float y = texture2D(texY, vTexCoord).r;
    float u = texture2D(texU, vTexCoord).r - 0.5;
    float v = texture2D(texV, vTexCoord).r - 0.5;
    float r = y + 1.402 * v;
    float g = y - 0.344136 * u - 0.714136 * v;
    float b = y + 1.772 * u;
    gl_FragColor = vec4(r, g, b, 1.0);
}
)";
    } // namespace

    VideoGLWidget::VideoGLWidget(QWidget *parent) : QOpenGLWidget(parent) {
    }

    VideoGLWidget::~VideoGLWidget() {
        // 释放 GL 资源必须在拥有上下文的情况下进行。
        makeCurrent();
        if (m_textures[0]) {
            glDeleteTextures(3, m_textures);
        }
        doneCurrent();
    }

    void VideoGLWidget::setFrame(const VideoFrame &frame) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending = frame;
            m_hasPending = true;
        }
        update();
    }

    void VideoGLWidget::clearFrame() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_pending = VideoFrame{};
            m_hasPending = false;
            m_hasFrame = false;
        }
        update();
    }

    void VideoGLWidget::initializeGL() {
        initializeOpenGLFunctions();
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        // 全屏切换会重建 GL 上下文，旧纹理 id 随之失效；这里复位状态，
        // 并将已有帧标记为待重传，避免切换后画面空白。
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_textures[0] = m_textures[1] = m_textures[2] = 0;
            m_texWidth = 0;
            m_texHeight = 0;
            m_hasFrame = false;
            if (m_pending.valid()) m_hasPending = true;
        }

        // 全屏切换会销毁旧上下文并新建上下文。着色器程序与上下文绑定，
        // 因此每次初始化都重建一个全新的程序对象，避免“重复定义 main”
        // 以及“program 与 shader 不属于同一上下文”的错误。
        delete m_program;
        m_program = new QOpenGLShaderProgram(this);

        if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)) {
            qWarning("VideoGLWidget: vertex shader compile failed: %s",
                     qPrintable(m_program->log()));
        }
        if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)) {
            qWarning("VideoGLWidget: fragment shader compile failed: %s",
                     qPrintable(m_program->log()));
        }
        if (!m_program->link()) {
            qWarning("VideoGLWidget: program link failed: %s", qPrintable(m_program->log()));
        }

        m_vertexAttr = m_program->attributeLocation("aPos");
        m_texCoordAttr = m_program->attributeLocation("aTexCoord");
    }

    void VideoGLWidget::ensureTextures() {
        if (!m_textures[0]) {
            glGenTextures(3, m_textures);
            for (GLuint tex : m_textures) {
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
        }
    }

    void VideoGLWidget::uploadTextures() {
        VideoFrame frame;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_hasPending) return;
            frame = m_pending;
            m_hasPending = false;
        }
        if (!frame.valid()) return;

        ensureTextures();
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        const int w = frame.width;
        const int h = frame.height;
        const int cw = w / 2;
        const int ch = h / 2;

        const bool sizeChanged = (w != m_texWidth || h != m_texHeight);
        m_texWidth = w;
        m_texHeight = h;

        struct Plane { GLuint tex; const uchar *data; int pw; int ph; };
        const Plane planes[3] = {
            {m_textures[0], reinterpret_cast<const uchar *>(frame.y.constData()), w, h},
            {m_textures[1], reinterpret_cast<const uchar *>(frame.u.constData()), cw, ch},
            {m_textures[2], reinterpret_cast<const uchar *>(frame.v.constData()), cw, ch},
        };

        for (const auto &p : planes) {
            glBindTexture(GL_TEXTURE_2D, p.tex);
            if (sizeChanged) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, p.pw, p.ph, 0,
                             GL_LUMINANCE, GL_UNSIGNED_BYTE, p.data);
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, p.pw, p.ph,
                                GL_LUMINANCE, GL_UNSIGNED_BYTE, p.data);
            }
        }
        m_hasFrame = true;
    }

    void VideoGLWidget::resizeGL(int w, int h) {
        glViewport(0, 0, w, h);
    }

    void VideoGLWidget::paintGL() {
        glClear(GL_COLOR_BUFFER_BIT);

        uploadTextures();
        if (!m_hasFrame || m_texWidth == 0 || !m_program || !m_program->isLinked()) {
            return;
        }

        // 保持视频宽高比，居中显示（letterbox）。
        const float widgetW = static_cast<float>(width());
        const float widgetH = static_cast<float>(height());
        const float videoAspect = static_cast<float>(m_texWidth) / static_cast<float>(m_texHeight);
        const float widgetAspect = widgetW / widgetH;

        float sx = 1.0f;
        float sy = 1.0f;
        if (widgetAspect > videoAspect) {
            sx = videoAspect / widgetAspect; // 两侧留黑
        } else {
            sy = widgetAspect / videoAspect; // 上下留黑
        }

        const GLfloat vertices[] = {
            -sx, -sy,
             sx, -sy,
            -sx,  sy,
             sx,  sy,
        };
        // 纹理坐标 Y 翻转（图像左上为原点）。
        const GLfloat texCoords[] = {
            0.0f, 1.0f,
            1.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
        };

        m_program->bind();

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_textures[0]);
        m_program->setUniformValue("texY", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_textures[1]);
        m_program->setUniformValue("texU", 1);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_textures[2]);
        m_program->setUniformValue("texV", 2);

        m_program->enableAttributeArray(m_vertexAttr);
        m_program->setAttributeArray(m_vertexAttr, vertices, 2);
        m_program->enableAttributeArray(m_texCoordAttr);
        m_program->setAttributeArray(m_texCoordAttr, texCoords, 2);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        m_program->disableAttributeArray(m_vertexAttr);
        m_program->disableAttributeArray(m_texCoordAttr);
        m_program->release();
    }

} // namespace Mixed::Player
