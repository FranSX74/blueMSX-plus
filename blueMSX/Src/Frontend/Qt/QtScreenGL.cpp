/*****************************************************************************
** QblueMSX - the emulated screen drawn by an OpenGL shader
**
** The software renderer still makes the picture (palette, monitor and PAL
** filters, colour controls) at zoom 2. The shader scales it to the window
** and draws the scanlines at the output resolution, as the Direct3D 12
** renderer of the Windows version does:
**   - scaling filter (Properties video.d3d.scalingFilter): nearest, sharp
**     bilinear (blends only across a one screen pixel seam, so pixels stay
**     even at any zoom), prescaled bilinear, bilinear;
**   - scanlines: mask = mix(depth, 1, sin^p) per MSX line, box filtered over
**     each screen pixel so there is no moire at fractional zooms, with the
**     brightness compensation and shape presets of the Windows dialog.
** Interlaced frames get no scanlines, as on a real set.
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
******************************************************************************
*/
#include "QtApp.h"

#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QPainter>

#include <cstdio>

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

namespace qbm {

static const char* vertexSource = R"(
attribute vec2 position;    /* clip space */
attribute vec2 source;      /* texels of the frame */
varying vec2 texel;
void main()
{
    texel = source;
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

static const char* fragmentSource = R"(
uniform sampler2D frame;
uniform vec2  frameSize;    /* texels */
uniform vec2  sourceMin;    /* the part shown, in texels */
uniform vec2  sourceMax;
uniform float filterMode;   /* P_D3D_SCALE_*: 0 nearest, 1 sharp, 2 prescaled, 3 bilinear */
uniform float texelsPerLine;/* texels per MSX line (the frame is zoom 2) */
uniform float scanlines;    /* 0 / 1 */
uniform float scanFloor;    /* brightness between lines, 0..1 */
uniform float scanShape;    /* sin^p exponent, 0..4 */
uniform float scanBoost;    /* brightness compensation, >= 1 */
varying vec2 texel;

vec3 fetch(vec2 t)
{
    t = clamp(t, sourceMin, sourceMax - 1.0);
    return texture2D(frame, (t + 0.5) / frameSize).rgb;
}

/* Integral of sin(pi * frac(t)) from 0 to t. */
float sinIntegral(float t)
{
    return floor(t) * 0.6366197724 + (1.0 - cos(3.14159265 * fract(t))) * 0.3183098862;
}

void main()
{
    vec3 color;
    if (filterMode > 0.5 && filterMode < 2.5) {
        vec2 q    = texel - 0.5;
        vec2 base = floor(q);
        vec2 f    = q - base;
        /* Sharp: the blend spans one screen pixel; prescaled: half a texel. */
        vec2 seam = filterMode < 1.5 ? max(fwidth(q), vec2(1e-5)) : vec2(0.5);
        vec2 w    = clamp((f - 0.5) / seam + 0.5, 0.0, 1.0);
        vec3 top  = mix(fetch(base), fetch(base + vec2(1.0, 0.0)), w.x);
        vec3 bot  = mix(fetch(base + vec2(0.0, 1.0)), fetch(base + vec2(1.0, 1.0)), w.x);
        color = mix(top, bot, w.y);
    }
    else {
        /* The texture filter (nearest or linear) is set for the mode. */
        color = texture2D(frame, clamp(texel, sourceMin, sourceMax) / frameSize).rgb;
    }

    if (scanlines > 0.5) {
        float line = texel.y / texelsPerLine;
        float dy   = abs(dFdy(line));
        float beam;
        if (dy < 1e-3) {
            beam = sin(fract(line) * 3.14159265);
        }
        else {
            /* Average over the screen pixel: no moire at any zoom. */
            beam = (sinIntegral(line + 0.5 * dy) - sinIntegral(line - 0.5 * dy)) / dy;
        }
        float shape = scanShape > 1e-4 ? pow(clamp(beam, 0.0, 1.0), scanShape) : 1.0;
        float mask  = mix(scanFloor, 1.0, shape);
        color *= pow(max(min(mask * scanBoost, 1.0), 0.0), 1.0 / 2.2);
    }
    gl_FragColor = vec4(color, 1.0);
}
)";

void ScreenWidget::initializeGL()
{
    initializeOpenGLFunctions();

    QOpenGLContext* ctx = context();
    bool es = ctx->isOpenGLES();
    QByteArray header = es ? "#version 100\n#extension GL_OES_standard_derivatives : enable\n"
                             "precision highp float;\n"
                           : "#version 120\n";

    program = new QOpenGLShaderProgram(this);
    bool ok = program->addShaderFromSourceCode(QOpenGLShader::Vertex, header + vertexSource) &&
              program->addShaderFromSourceCode(QOpenGLShader::Fragment, header + fragmentSource);
    program->bindAttributeLocation("position", 0);
    program->bindAttributeLocation("source", 1);
    ok = ok && program->link();
    if (!ok) {
        fprintf(stderr, "qbluemsx: no shader renderer, drawing with QPainter: %s\n",
                program->log().toUtf8().constData());
        delete program;
        program = nullptr;
        platformSetGpuScanlines(0);
        return;
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame.width(), frame.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    frameDirty = true;
    platformSetGpuScanlines(1);
}

void ScreenWidget::paintGL()
{
    int w = width(), h = height();
    int sw = w, sh = w * 3 / 4;
    if (sh > h) {
        sh = h;
        sw = h * 4 / 3;
    }
    screenArea = QRect((w - sw) / 2, (h - sh) / 2, sw, sh);

    if (!drawWithShader()) drawWithPainter();
}

void ScreenWidget::drawWithPainter()
{
    int sx, sy, sxw, syh;
    platformScreenSourceRect(QBM_RENDER_ZOOM, &sx, &sy, &sxw, &syh);
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(screenArea, frame, QRect(sx, sy, sxw, syh));
}

bool ScreenWidget::drawWithShader()
{
    if (program == nullptr) return false;
    const Properties* props = platformGetProperties();
    int filter = props ? props->video.d3d.scalingFilter : P_D3D_SCALE_SHARP;

    glBindTexture(GL_TEXTURE_2D, texture);
    if (frameDirty) {
        if (context()->isOpenGLES()) {
            QImage rgba = frame.convertToFormat(QImage::Format_RGBA8888);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rgba.width(), rgba.height(), GL_RGBA, GL_UNSIGNED_BYTE,
                            rgba.constBits());
        }
        else {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width(), frame.height(), GL_BGRA, GL_UNSIGNED_BYTE,
                            frame.constBits());
        }
        frameDirty = false;
    }
    GLint texFilter = filter == P_D3D_SCALE_BILINEAR ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texFilter);

    qreal dpr = devicePixelRatioF();
    glViewport(0, 0, qRound(width() * dpr), qRound(height() * dpr));
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport(qRound(screenArea.x() * dpr), qRound((height() - screenArea.bottom() - 1) * dpr),
               qRound(screenArea.width() * dpr), qRound(screenArea.height() * dpr));

    int sx, sy, sxw, syh;
    platformScreenSourceRect(QBM_RENDER_ZOOM, &sx, &sy, &sxw, &syh);
    const GLfloat position[] = { -1, 1,   1, 1,   -1, -1,   1, -1 };
    const GLfloat source[] = {
        GLfloat(sx), GLfloat(sy),               GLfloat(sx + sxw), GLfloat(sy),
        GLfloat(sx), GLfloat(sy + syh),         GLfloat(sx + sxw), GLfloat(sy + syh),
    };

    bool scan = props && props->video.scanlinesEnable && !platformLastFrameInterlaced();
    float boost = 1.0f;
    if (scan) {
        boost = props->video.scanlinesBrightAuto ? 2.0f
                                                 : qBound(100, props->video.scanlinesBrightPct, 200) / 100.0f;
    }

    program->bind();
    program->setUniformValue("frame", 0);
    program->setUniformValue("frameSize", QVector2D(frame.width(), frame.height()));
    program->setUniformValue("sourceMin", QVector2D(sx, sy));
    program->setUniformValue("sourceMax", QVector2D(sx + sxw, sy + syh));
    program->setUniformValue("filterMode", GLfloat(filter));
    program->setUniformValue("texelsPerLine", GLfloat(QBM_RENDER_ZOOM));
    program->setUniformValue("scanlines", scan ? 1.0f : 0.0f);
    program->setUniformValue("scanFloor", props ? qBound(0, props->video.scanlinesPct, 100) / 100.0f : 0.0f);
    program->setUniformValue("scanShape", props ? qBound(0, props->video.scanlinesShapePct, 100) / 25.0f : 2.0f);
    program->setUniformValue("scanBoost", boost);

    program->enableAttributeArray(0);
    program->enableAttributeArray(1);
    program->setAttributeArray(0, GL_FLOAT, position, 2);
    program->setAttributeArray(1, GL_FLOAT, source, 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    program->disableAttributeArray(0);
    program->disableAttributeArray(1);
    program->release();
    return true;
}

}
