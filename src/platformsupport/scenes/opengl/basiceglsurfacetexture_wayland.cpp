/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "platformsupport/scenes/opengl/basiceglsurfacetexture_wayland.h"
#include "core/graphicsbufferview.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/gltexture.h"
#include "platformsupport/scenes/opengl/abstract_egl_backend.h"
#include "utils/common.h"

#include "abstract_egl_backend.h"
#include <epoxy/egl.h>
#include <utils/drm_format_helper.h>

namespace KWin
{

BasicEGLSurfaceTextureWayland::BasicEGLSurfaceTextureWayland(OpenGLBackend *backend, SurfacePixmap *pixmap)
    : OpenGLSurfaceTextureWayland(backend, pixmap)
{
}

BasicEGLSurfaceTextureWayland::~BasicEGLSurfaceTextureWayland()
{
    destroy();
}

AbstractEglBackend *BasicEGLSurfaceTextureWayland::backend() const
{
    return static_cast<AbstractEglBackend *>(m_backend);
}

bool BasicEGLSurfaceTextureWayland::create()
{
    if (m_pixmap->buffer()->dmabufAttributes()) {
        return loadDmabufTexture(m_pixmap->buffer());
    } else if (m_pixmap->buffer()->shmAttributes()) {
        return loadShmTexture(m_pixmap->buffer());
    } else {
        return false;
    }
}

void BasicEGLSurfaceTextureWayland::destroy()
{
    m_texture.reset();
    m_bufferType = BufferType::None;
}

void BasicEGLSurfaceTextureWayland::update(const QRegion &region)
{
    if (m_pixmap->buffer()->dmabufAttributes()) {
        updateDmabufTexture(m_pixmap->buffer());
    } else if (m_pixmap->buffer()->shmAttributes()) {
        updateShmTexture(m_pixmap->buffer(), region);
    }
}

bool BasicEGLSurfaceTextureWayland::loadShmTexture(GraphicsBuffer *buffer)
{
    const GraphicsBufferView view(buffer);
    if (Q_UNLIKELY(!view.image())) {
        return false;
    }

    std::shared_ptr<GLTexture> texture = GLTexture::upload(*view.image());
    if (Q_UNLIKELY(!texture)) {
        return false;
    }

    texture->setFilter(GL_LINEAR);
    texture->setWrapMode(GL_CLAMP_TO_EDGE);
    texture->setContentTransform(TextureTransform::MirrorY);

    m_texture = {{texture}};

    m_bufferType = BufferType::Shm;

    return true;
}

void BasicEGLSurfaceTextureWayland::updateShmTexture(GraphicsBuffer *buffer, const QRegion &region)
{
    if (Q_UNLIKELY(m_bufferType != BufferType::Shm)) {
        destroy();
        create();
        return;
    }

    const GraphicsBufferView view(buffer);
    if (Q_UNLIKELY(!view.image())) {
        return;
    }

    for (const QRect &rect : region) {
        m_texture.planes[0]->update(*view.image(), rect.topLeft(), rect);
    }
}

bool BasicEGLSurfaceTextureWayland::loadDmabufTexture(GraphicsBuffer *buffer)
{
    auto createTexture = [this](EGLImageKHR image, const QSize &size) {
        if (Q_UNLIKELY(image == EGL_NO_IMAGE_KHR)) {
            qCritical(KWIN_OPENGL) << "Invalid dmabuf-based wl_buffer";
            return std::shared_ptr<GLTexture>();
        }

        auto texture = std::make_shared<GLTexture>(GL_TEXTURE_2D);
        texture->setSize(size);
        texture->create();
        texture->setWrapMode(GL_CLAMP_TO_EDGE);
        texture->setFilter(GL_LINEAR);
        texture->bind();
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, static_cast<GLeglImageOES>(image));
        texture->unbind();
        if (m_pixmap->bufferOrigin() == GraphicsBufferOrigin::TopLeft) {
            texture->setContentTransform(TextureTransform::MirrorY);
        }
        return texture;
    };

    auto format = formatInfo(buffer->dmabufAttributes()->format);
    if (format && format->yuvConversion) {
        QList<std::shared_ptr<GLTexture>> textures;
        Q_ASSERT(format->yuvConversion->plane.count() == uint(buffer->dmabufAttributes()->planeCount));
        for (uint plane = 0; plane < format->yuvConversion->plane.count(); ++plane) {
            const auto &currentPlane = format->yuvConversion->plane[plane];
            QSize size = buffer->size();
            size.rwidth() /= currentPlane.widthDivisor;
            size.rheight() /= currentPlane.heightDivisor;

            auto t = createTexture(backend()->importBufferAsImage(buffer, plane, currentPlane.format, size), size);
            if (!t) {
                return false;
            }
            textures << t;
        }
        m_texture = {textures};
    } else {
        auto texture = createTexture(backend()->importBufferAsImage(buffer), buffer->size());
        if (!texture) {
            return false;
        }
        m_texture = {{texture}};
    }
    m_bufferType = BufferType::DmaBuf;

    return true;
}

void BasicEGLSurfaceTextureWayland::updateDmabufTexture(GraphicsBuffer *buffer)
{
    if (Q_UNLIKELY(m_bufferType != BufferType::DmaBuf)) {
        destroy();
        create();
        return;
    }

    const GLint target = GL_TEXTURE_2D;
    auto format = formatInfo(buffer->dmabufAttributes()->format);
    if (format && format->yuvConversion) {
        Q_ASSERT(format->yuvConversion->plane.count() == uint(buffer->dmabufAttributes()->planeCount));
        for (uint plane = 0; plane < format->yuvConversion->plane.count(); ++plane) {
            const auto &currentPlane = format->yuvConversion->plane[plane];
            QSize size = buffer->size();
            size.rwidth() /= currentPlane.widthDivisor;
            size.rheight() /= currentPlane.heightDivisor;

            m_texture.planes[plane]->bind();
            glEGLImageTargetTexture2DOES(target, static_cast<GLeglImageOES>(backend()->importBufferAsImage(buffer, plane, currentPlane.format, size)));
            m_texture.planes[plane]->unbind();
        }
    } else {
        Q_ASSERT(m_texture.planes.count() == 1);
        m_texture.planes[0]->bind();
        glEGLImageTargetTexture2DOES(target, static_cast<GLeglImageOES>(backend()->importBufferAsImage(buffer)));
        m_texture.planes[0]->unbind();
    }
}

} // namespace KWin
