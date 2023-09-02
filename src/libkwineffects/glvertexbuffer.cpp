/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2006-2007 Rivo Laks <rivolaks@hot.ee>
    SPDX-FileCopyrightText: 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2023 Xaver Hugl <xaver.hugl@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "glvertexbuffer.h"
#include "glframebuffer.h"
#include "glplatform.h"
#include "glshader.h"
#include "glshadermanager.h"
#include "glutils.h"
#include "kwineffects.h"
#include "logging_p.h"

#include <QVector4D>
#include <bitset>
#include <deque>

namespace KWin
{

// Certain GPUs, especially mobile, require the data copied to the GPU to be aligned to a
// certain amount of bytes. For example, the Mali GPU requires data to be aligned to 8 bytes.
// This function helps ensure that the data is aligned.
template<typename T>
T align(T value, int bytes)
{
    return (value + bytes - 1) & ~T(bytes - 1);
}

class IndexBuffer
{
public:
    IndexBuffer();
    ~IndexBuffer();

    void accommodate(size_t count);
    void bind();

private:
    GLuint m_buffer;
    size_t m_count = 0;
    std::vector<uint16_t> m_data;
};

IndexBuffer::IndexBuffer()
{
    // The maximum number of quads we can render with 16 bit indices is 16,384.
    // But we start with 512 and grow the buffer as needed.
    glGenBuffers(1, &m_buffer);
    accommodate(512);
}

IndexBuffer::~IndexBuffer()
{
    glDeleteBuffers(1, &m_buffer);
}

void IndexBuffer::accommodate(size_t count)
{
    // Check if we need to grow the buffer.
    if (count <= m_count) {
        return;
    }
    Q_ASSERT(m_count * 2 < std::numeric_limits<uint16_t>::max() / 4);
    const size_t oldCount = m_count;
    m_count *= 2;
    m_data.reserve(m_count * 6);
    for (size_t i = oldCount; i < m_count; i++) {
        const uint16_t offset = i * 4;
        m_data[i * 6 + 0] = offset + 1;
        m_data[i * 6 + 1] = offset + 0;
        m_data[i * 6 + 2] = offset + 3;
        m_data[i * 6 + 3] = offset + 3;
        m_data[i * 6 + 4] = offset + 2;
        m_data[i * 6 + 5] = offset + 1;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_count * sizeof(uint16_t), m_data.data(), GL_STATIC_DRAW);
}

void IndexBuffer::bind()
{
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_buffer);
}

// ------------------------------------------------------------------

struct VertexAttrib
{
    int size;
    GLenum type;
    int offset;
};

class GLVertexBufferPrivate
{
public:
    GLVertexBufferPrivate(GLVertexBuffer::UsageHint usageHint)
    {
        glGenBuffers(1, &buffer);

        switch (usageHint) {
        case GLVertexBuffer::Dynamic:
            usage = GL_DYNAMIC_DRAW;
            break;
        case GLVertexBuffer::Static:
            usage = GL_STATIC_DRAW;
            break;
        default:
            usage = GL_STREAM_DRAW;
            break;
        }
    }

    ~GLVertexBufferPrivate()
    {
        if (buffer != 0) {
            glDeleteBuffers(1, &buffer);
        }
    }

    void bindArrays();
    void unbindArrays();

    GLuint buffer = 0;
    GLenum usage = 0;
    int vertexCount = 0;
    static bool haveBufferStorage;
    static bool supportsIndexedQuads;
    std::array<VertexAttrib, VertexAttributeCount> attrib;
    size_t attribStride = 0;
    std::bitset<32> enabledArrays;
    static std::unique_ptr<IndexBuffer> s_indexBuffer;
};

bool GLVertexBufferPrivate::supportsIndexedQuads = false;
bool GLVertexBufferPrivate::haveBufferStorage = false;
std::unique_ptr<IndexBuffer> GLVertexBufferPrivate::s_indexBuffer;

void GLVertexBufferPrivate::bindArrays()
{
    glBindBuffer(GL_ARRAY_BUFFER, buffer);

    for (size_t i = 0; i < enabledArrays.size(); i++) {
        if (enabledArrays[i]) {
            glVertexAttribPointer(i, attrib[i].size, attrib[i].type, GL_FALSE, attribStride, reinterpret_cast<void *>(attrib[i].offset));
            glEnableVertexAttribArray(i);
        }
    }
}

void GLVertexBufferPrivate::unbindArrays()
{
    for (size_t i = 0; i < enabledArrays.size(); i++) {
        if (enabledArrays[i]) {
            glDisableVertexAttribArray(i);
        }
    }
}

GLVertexBuffer::GLVertexBuffer(UsageHint hint)
    : d(std::make_unique<GLVertexBufferPrivate>(hint))
{
}

GLVertexBuffer::~GLVertexBuffer() = default;

void GLVertexBuffer::setData(const void *data, size_t size)
{
    glBindBuffer(GL_ARRAY_BUFFER, d->buffer);
    glBufferData(GL_ARRAY_BUFFER, size, data, d->usage);
}

void GLVertexBuffer::setVertexCount(int count)
{
    d->vertexCount = count;
}

void GLVertexBuffer::setAttribLayout(std::span<const GLVertexAttrib> attribs, size_t stride)
{
    d->enabledArrays.reset();
    for (const auto &attrib : attribs) {
        Q_ASSERT(attrib.attributeIndex < d->attrib.size());
        d->attrib[attrib.attributeIndex].size = attrib.componentCount;
        d->attrib[attrib.attributeIndex].type = attrib.type;
        d->attrib[attrib.attributeIndex].offset = attrib.relativeOffset;
        d->enabledArrays[attrib.attributeIndex] = true;
    }
    d->attribStride = stride;
}

void GLVertexBuffer::render(GLenum primitiveMode)
{
    render(infiniteRegion(), primitiveMode, false);
}

void GLVertexBuffer::render(const QRegion &region, GLenum primitiveMode, bool hardwareClipping)
{
    d->bindArrays();
    draw(region, primitiveMode, 0, d->vertexCount, hardwareClipping);
    d->unbindArrays();
}

void GLVertexBuffer::bindArrays()
{
    d->bindArrays();
}

void GLVertexBuffer::unbindArrays()
{
    d->unbindArrays();
}

void GLVertexBuffer::draw(GLenum primitiveMode, int first, int count)
{
    draw(infiniteRegion(), primitiveMode, first, count, false);
}

void GLVertexBuffer::draw(const QRegion &region, GLenum primitiveMode, int first, int count, bool hardwareClipping)
{
    if (primitiveMode == GL_QUADS) {
        if (!GLVertexBufferPrivate::s_indexBuffer) {
            GLVertexBufferPrivate::s_indexBuffer = std::make_unique<IndexBuffer>();
        }

        GLVertexBufferPrivate::s_indexBuffer->bind();
        GLVertexBufferPrivate::s_indexBuffer->accommodate(count / 4);

        count = count * 6 / 4;

        if (!hardwareClipping) {
            glDrawElementsBaseVertex(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, nullptr, first);
        } else {
            // Clip using scissoring
            const GLFramebuffer *current = GLFramebuffer::currentFramebuffer();
            for (const QRect &r : region) {
                glScissor(r.x(), current->size().height() - (r.y() + r.height()), r.width(), r.height());
                glDrawElementsBaseVertex(GL_TRIANGLES, count, GL_UNSIGNED_SHORT, nullptr, first);
            }
        }
        return;
    }

    if (!hardwareClipping) {
        glDrawArrays(primitiveMode, first, count);
    } else {
        // Clip using scissoring
        const GLFramebuffer *current = GLFramebuffer::currentFramebuffer();
        for (const QRect &r : region) {
            glScissor(r.x(), current->size().height() - (r.y() + r.height()), r.width(), r.height());
            glDrawArrays(primitiveMode, first, count);
        }
    }
}

bool GLVertexBuffer::supportsIndexedQuads()
{
    return GLVertexBufferPrivate::supportsIndexedQuads;
}

void GLVertexBuffer::initStatic()
{
    if (GLPlatform::instance()->isGLES()) {
        bool haveBaseVertex = hasGLExtension(QByteArrayLiteral("GL_OES_draw_elements_base_vertex"));
        bool haveCopyBuffer = hasGLVersion(3, 0);
        bool haveMapBufferRange = hasGLExtension(QByteArrayLiteral("GL_EXT_map_buffer_range"));

        GLVertexBufferPrivate::supportsIndexedQuads = haveBaseVertex && haveCopyBuffer && haveMapBufferRange;
        GLVertexBufferPrivate::haveBufferStorage = hasGLExtension("GL_EXT_buffer_storage");
    } else {
        bool haveBaseVertex = hasGLVersion(3, 2) || hasGLExtension(QByteArrayLiteral("GL_ARB_draw_elements_base_vertex"));
        bool haveCopyBuffer = hasGLVersion(3, 1) || hasGLExtension(QByteArrayLiteral("GL_ARB_copy_buffer"));
        bool haveMapBufferRange = hasGLVersion(3, 0) || hasGLExtension(QByteArrayLiteral("GL_ARB_map_buffer_range"));

        GLVertexBufferPrivate::supportsIndexedQuads = haveBaseVertex && haveCopyBuffer && haveMapBufferRange;
        GLVertexBufferPrivate::haveBufferStorage = hasGLVersion(4, 4) || hasGLExtension("GL_ARB_buffer_storage");
    }
    GLVertexBufferPrivate::s_indexBuffer.reset();
}

void GLVertexBuffer::cleanup()
{
    GLVertexBufferPrivate::s_indexBuffer.reset();
    GLVertexBufferPrivate::supportsIndexedQuads = false;
}

}
