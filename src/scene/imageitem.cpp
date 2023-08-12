/*
    SPDX-FileCopyrightText: 2022 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "scene/imageitem.h"

#include "libkwineffects/kwingltexture.h"
#include "platformsupport/scenes/vulkan/vulkan_texture.h"

namespace KWin
{

ImageItem::ImageItem(Scene *scene, Item *parent)
    : Item(scene, parent)
{
}

QImage ImageItem::image() const
{
    return m_image;
}

void ImageItem::setImage(const QImage &image)
{
    m_image = image;
}

ImageItemOpenGL::ImageItemOpenGL(Scene *scene, Item *parent)
    : ImageItem(scene, parent)
{
}

ImageItemOpenGL::~ImageItemOpenGL()
{
}

GLTexture *ImageItemOpenGL::texture() const
{
    return m_texture.get();
}

void ImageItemOpenGL::preprocess()
{
    if (m_image.isNull()) {
        m_texture.reset();
        m_textureKey = 0;
    } else if (m_textureKey != m_image.cacheKey()) {
        m_textureKey = m_image.cacheKey();

        if (!m_texture || m_texture->size() != m_image.size()) {
            m_texture = GLTexture::upload(m_image);
            m_texture->setFilter(GL_LINEAR);
            m_texture->setWrapMode(GL_CLAMP_TO_EDGE);
        } else {
            m_texture->update(m_image);
        }
    }
}

WindowQuadList ImageItemOpenGL::buildQuads() const
{
    const QRectF geometry = boundingRect();
    if (geometry.isEmpty()) {
        return WindowQuadList{};
    }

    WindowQuad quad;
    quad[0] = WindowVertex(geometry.topLeft(), QPointF(0, 0));
    quad[1] = WindowVertex(geometry.topRight(), QPointF(1, 0));
    quad[2] = WindowVertex(geometry.bottomRight(), QPointF(1, 1));
    quad[3] = WindowVertex(geometry.bottomLeft(), QPointF(0, 1));

    WindowQuadList ret;
    ret.append(quad);
    return ret;
}

ImageItemVulkan::ImageItemVulkan(Scene *scene, Item *parent)
    : ImageItem(scene, parent)
{
}

ImageItemVulkan::~ImageItemVulkan() = default;

void ImageItemVulkan::preprocess()
{
    if (m_image.isNull()) {
        m_texture.reset();
        m_textureKey.reset();
    } else if (m_textureKey != m_image.cacheKey()) {
        m_textureKey = m_image.cacheKey();
        m_texture = VulkanTexture::upload(m_image);
    }
}

WindowQuadList ImageItemVulkan::buildQuads() const
{
    const QRectF geometry = boundingRect();
    if (geometry.isEmpty()) {
        return WindowQuadList{};
    }

    WindowQuad quad;
    quad[0] = WindowVertex(geometry.topLeft(), QPointF(0, 0));
    quad[1] = WindowVertex(geometry.topRight(), QPointF(1, 0));
    quad[2] = WindowVertex(geometry.bottomRight(), QPointF(1, 1));
    quad[3] = WindowVertex(geometry.bottomLeft(), QPointF(0, 1));

    WindowQuadList ret;
    ret.append(quad);
    return ret;
}

VulkanTexture *ImageItemVulkan::texture() const
{
    return m_texture.get();
}

} // namespace KWin

#include "moc_imageitem.cpp"
