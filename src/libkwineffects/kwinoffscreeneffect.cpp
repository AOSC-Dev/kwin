/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwinoffscreeneffect.h"
#include "kwingltexture.h"
#include "kwinglutils.h"

namespace KWin
{

struct OffscreenData
{
public:
    void setDirty();
    void setShader(GLShader *newShader);

    void paint(EffectWindow *window, const QRegion &region,
               const WindowPaintData &data, const WindowQuadList &quads);

    void maybeRender(EffectWindow *window);

private:
    std::unique_ptr<GLTexture> m_texture;
    std::unique_ptr<GLFramebuffer> m_fbo;
    bool m_isDirty = true;
    GLShader *m_shader = nullptr;
};

class OffscreenEffectPrivate
{
public:
    QHash<EffectWindow *, OffscreenData *> windows;
    QMetaObject::Connection windowDamagedConnection;
    QMetaObject::Connection windowDeletedConnection;
};

class CrossFadeEffectPrivate
{
public:
    QHash<EffectWindow *, OffscreenData *> windows;
    qreal progress;
};

OffscreenEffect::OffscreenEffect(QObject *parent)
    : Effect(parent)
    , d(new OffscreenEffectPrivate)
{
}

OffscreenEffect::~OffscreenEffect()
{
    qDeleteAll(d->windows);
}

bool OffscreenEffect::supported()
{
    return effects->isOpenGLCompositing();
}

void OffscreenEffect::redirect(EffectWindow *window)
{
    OffscreenData *&offscreenData = d->windows[window];
    if (offscreenData) {
        return;
    }
    offscreenData = new OffscreenData;

    if (d->windows.count() == 1) {
        setupConnections();
    }
}

void OffscreenEffect::unredirect(EffectWindow *window)
{
    delete d->windows.take(window);
    if (d->windows.isEmpty()) {
        destroyConnections();
    }
}

void OffscreenEffect::apply(EffectWindow *window, int mask, WindowPaintData &data, WindowQuadList &quads)
{
    Q_UNUSED(window)
    Q_UNUSED(mask)
    Q_UNUSED(data)
    Q_UNUSED(quads)
}

void OffscreenData::maybeRender(EffectWindow *window)
{
    const QRect geometry = window->expandedGeometry().toAlignedRect();
    QSize textureSize = geometry.size();

    if (const EffectScreen *screen = window->screen()) {
        textureSize *= screen->devicePixelRatio();
    }

    if (!m_texture || m_texture->size() != textureSize) {
        m_texture.reset(new GLTexture(GL_RGBA8, textureSize));
        m_texture->setFilter(GL_LINEAR);
        m_texture->setWrapMode(GL_CLAMP_TO_EDGE);
        m_fbo.reset(new GLFramebuffer(m_texture.get()));
        m_isDirty = true;
    }

    if (m_isDirty) {
        GLFramebuffer::pushFramebuffer(m_fbo.get());
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);

        QMatrix4x4 projectionMatrix;
        projectionMatrix.ortho(QRect(0, 0, geometry.width(), geometry.height()));

        WindowPaintData data;
        data.setXTranslation(-geometry.x());
        data.setYTranslation(-geometry.y());
        data.setOpacity(1.0);
        data.setProjectionMatrix(projectionMatrix);

        const int mask = Effect::PAINT_WINDOW_TRANSFORMED | Effect::PAINT_WINDOW_TRANSLUCENT;
        effects->drawWindow(window, mask, infiniteRegion(), data);

        GLFramebuffer::popFramebuffer();
        m_isDirty = false;
    }
}

void OffscreenData::setDirty()
{
    m_isDirty = true;
}

void OffscreenData::setShader(GLShader *newShader)
{
    m_shader = newShader;
}

void OffscreenData::paint(EffectWindow *window, const QRegion &region,
                          const WindowPaintData &data, const WindowQuadList &quads)
{
    GLShader *shader = m_shader ? m_shader : ShaderManager::instance()->shader(ShaderTrait::MapTexture | ShaderTrait::Modulate | ShaderTrait::AdjustSaturation);
    ShaderBinder binder(shader);

    const bool indexedQuads = GLVertexBuffer::supportsIndexedQuads();
    const GLenum primitiveType = indexedQuads ? GL_QUADS : GL_TRIANGLES;
    const int verticesPerQuad = indexedQuads ? 4 : 6;

    const GLVertexAttrib attribs[] = {
        {VA_Position, 2, GL_FLOAT, offsetof(GLVertex2D, position)},
        {VA_TexCoord, 2, GL_FLOAT, offsetof(GLVertex2D, texcoord)},
    };

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(attribs, 2, sizeof(GLVertex2D));
    const size_t size = verticesPerQuad * quads.count() * sizeof(GLVertex2D);
    GLVertex2D *map = static_cast<GLVertex2D *>(vbo->map(size));

    quads.makeInterleavedArrays(primitiveType, map, m_texture->matrix(NormalizedCoordinates));
    vbo->unmap();
    vbo->bindArrays();

    const qreal rgb = data.brightness() * data.opacity();
    const qreal a = data.opacity();

    QMatrix4x4 mvp = data.screenProjectionMatrix();
    mvp.translate(window->x(), window->y());

    shader->setUniform(GLShader::ModelViewProjectionMatrix, mvp * data.toMatrix());
    shader->setUniform(GLShader::ModulationConstant, QVector4D(rgb, rgb, rgb, a));
    shader->setUniform(GLShader::Saturation, data.saturation());
    shader->setUniform(GLShader::TextureWidth, m_texture->width());
    shader->setUniform(GLShader::TextureHeight, m_texture->height());

    const bool clipping = region != infiniteRegion();
    const QRegion clipRegion = clipping ? effects->mapToRenderTarget(region) : infiniteRegion();

    if (clipping) {
        glEnable(GL_SCISSOR_TEST);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_texture->bind();
    vbo->draw(clipRegion, primitiveType, 0, verticesPerQuad * quads.count(), clipping);
    m_texture->unbind();

    glDisable(GL_BLEND);
    if (clipping) {
        glDisable(GL_SCISSOR_TEST);
    }
    vbo->unbindArrays();
}

void OffscreenEffect::drawWindow(EffectWindow *window, int mask, const QRegion &region, WindowPaintData &data)
{
    OffscreenData *offscreenData = d->windows.value(window);
    if (!offscreenData) {
        effects->drawWindow(window, mask, region, data);
        return;
    }

    const QRectF expandedGeometry = window->expandedGeometry();
    const QRectF frameGeometry = window->frameGeometry();

    QRectF visibleRect = expandedGeometry;
    visibleRect.moveTopLeft(expandedGeometry.topLeft() - frameGeometry.topLeft());
    WindowQuad quad;
    quad[0] = WindowVertex(visibleRect.topLeft(), QPointF(0, 0));
    quad[1] = WindowVertex(visibleRect.topRight(), QPointF(1, 0));
    quad[2] = WindowVertex(visibleRect.bottomRight(), QPointF(1, 1));
    quad[3] = WindowVertex(visibleRect.bottomLeft(), QPointF(0, 1));

    WindowQuadList quads;
    quads.append(quad);
    apply(window, mask, data, quads);

    offscreenData->maybeRender(window);
    offscreenData->paint(window, region, data, quads);
}

void OffscreenEffect::handleWindowDamaged(EffectWindow *window)
{
    OffscreenData *offscreenData = d->windows.value(window);
    if (offscreenData) {
        offscreenData->setDirty();
    }
}

void OffscreenEffect::handleWindowDeleted(EffectWindow *window)
{
    unredirect(window);
}

void OffscreenEffect::setupConnections()
{
    d->windowDamagedConnection =
        connect(effects, &EffectsHandler::windowDamaged, this, &OffscreenEffect::handleWindowDamaged);

    d->windowDeletedConnection =
        connect(effects, &EffectsHandler::windowDeleted, this, &OffscreenEffect::handleWindowDeleted);
}

void OffscreenEffect::destroyConnections()
{
    disconnect(d->windowDamagedConnection);
    disconnect(d->windowDeletedConnection);

    d->windowDamagedConnection = {};
    d->windowDeletedConnection = {};
}

CrossFadeEffect::CrossFadeEffect(QObject *parent)
    : Effect(parent)
    , d(new CrossFadeEffectPrivate)
{
}

CrossFadeEffect::~CrossFadeEffect()
{
    qDeleteAll(d->windows);
}

void CrossFadeEffect::drawWindow(EffectWindow *window, int mask, const QRegion &region, WindowPaintData &data)
{
    Q_UNUSED(mask)

    // paint the new window (if applicable) underneath
    Effect::drawWindow(window, mask, region, data);

    // paint old snapshot on top
    WindowPaintData previousWindowData = data;
    previousWindowData.setOpacity((1.0 - data.crossFadeProgress()) * data.opacity());
    OffscreenData *offscreenData = d->windows[window];
    if (!offscreenData) {
        return;
    }

    const QRectF expandedGeometry = window->expandedGeometry();
    const QRectF frameGeometry = window->frameGeometry();

    QRectF visibleRect = expandedGeometry;
    visibleRect.moveTopLeft(expandedGeometry.topLeft() - frameGeometry.topLeft());
    WindowQuad quad;
    quad[0] = WindowVertex(visibleRect.topLeft(), QPointF(0, 0));
    quad[1] = WindowVertex(visibleRect.topRight(), QPointF(1, 0));
    quad[2] = WindowVertex(visibleRect.bottomRight(), QPointF(1, 1));
    quad[3] = WindowVertex(visibleRect.bottomLeft(), QPointF(0, 1));

    WindowQuadList quads;
    quads.append(quad);
    offscreenData->paint(window, region, previousWindowData, quads);
}

void CrossFadeEffect::redirect(EffectWindow *window)
{
    OffscreenData *&offscreenData = d->windows[window];
    if (offscreenData) {
        return;
    }
    offscreenData = new OffscreenData;

    effects->makeOpenGLContextCurrent();
    offscreenData->maybeRender(window);
}

void CrossFadeEffect::unredirect(EffectWindow *window)
{
    delete d->windows.take(window);
}

void CrossFadeEffect::setShader(EffectWindow *window, GLShader *shader)
{
    OffscreenData *offscreenData = d->windows.value(window);
    if (offscreenData) {
        offscreenData->setShader(shader);
    }
}

} // namespace KWin
