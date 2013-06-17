/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#ifndef KWIN_EGL_WAYLAND_BACKEND_H
#define KWIN_EGL_WAYLAND_BACKEND_H
#include "scene_opengl.h"
// wayland
#include <wayland-egl.h>
// xcb
#include <xcb/shm.h>

class QTemporaryFile;
struct wl_buffer;
struct wl_shm;

namespace KWin
{

namespace Wayland {
    class WaylandBackend;
}

class Shm;

/**
 * @brief OpenGL Backend using Egl on a Wayland surface.
 *
 * This Backend is the basis for a session compositor running on top of a Wayland system compositor.
 * It creates a Surface as large as the screen and maps it as a fullscreen shell surface on the
 * system compositor. The OpenGL context is created on the Wayland surface, so for rendering X11 is
 * not involved.
 *
 * At the moment the backend is still rather limited. For getting textures from pixmap it uses the
 * XShm library. This is currently a hack and only as proof of concept till we support texture from
 * Wayland buffers. From then on we should use XWayland for texture mapping.
 *
 * Also in repainting the backend is currently still rather limited. Only supported mode is fullscreen
 * repaints, which is obviously not optimal. Best solution is probably to go for buffer_age extension
 * and make it the only available solution next to fullscreen repaints.
 **/
class EglWaylandBackend : public QObject, public OpenGLBackend
{
    Q_OBJECT
public:
    EglWaylandBackend();
    virtual ~EglWaylandBackend();
    virtual void screenGeometryChanged(const QSize &size);
    virtual SceneOpenGL::TexturePrivate *createBackendTexture(SceneOpenGL::Texture *texture);
    virtual QRegion prepareRenderingFrame();
    virtual void endRenderingFrame(const QRegion &renderedRegion, const QRegion &damagedRegion);
    virtual bool makeCurrent() override;
    virtual void doneCurrent() override;
    Shm *shm();

protected:
    virtual void present();

private Q_SLOTS:
    void overlaySizeChanged(const QSize &size);

private:
    void init();
    bool initializeEgl();
    bool initBufferConfigs();
    bool initRenderingContext();
    bool makeContextCurrent();
    EGLDisplay m_display;
    EGLConfig m_config;
    EGLSurface m_surface;
    EGLContext m_context;
    int m_bufferAge;
    Wayland::WaylandBackend *m_wayland;
    wl_egl_window *m_overlay;
    QScopedPointer<Shm> m_shm;
    friend class EglWaylandTexture;
};

/**
 * @brief Texture using an EGLImageKHR.
 **/
class EglWaylandTexture : public SceneOpenGL::TexturePrivate
{
public:
    virtual ~EglWaylandTexture();
    virtual void findTarget();
    virtual bool loadTexture(const Pixmap& pix, const QSize& size, int depth);
    virtual OpenGLBackend *backend();
    virtual bool update(const QRegion &damage);

private:
    friend class EglWaylandBackend;
    EglWaylandTexture(SceneOpenGL::Texture *texture, EglWaylandBackend *backend);
    SceneOpenGL::Texture *q;
    EglWaylandBackend *m_backend;
    /**
     * The Pixmap of the window content. Get's updated in loadTexture.
     */
    xcb_pixmap_t m_referencedPixmap;
};

/**
 * @brief Small helper class to encapsulate SHM related functionality.
 *
 */
class Shm
{
public:
    Shm();
    ~Shm();
    int shmId() const;
    void *buffer() const;
    xcb_shm_seg_t segment() const;
    bool isValid() const;
private:
    bool init();
    int m_shmId;
    void *m_buffer;
    xcb_shm_seg_t m_segment;
    bool m_valid;
};

inline
void *Shm::buffer() const
{
    return m_buffer;
}

inline
bool Shm::isValid() const
{
    return m_valid;
}

inline
xcb_shm_seg_t Shm::segment() const
{
    return m_segment;
}

inline
int Shm::shmId() const
{
    return m_shmId;
}

} // namespace

#endif //  KWIN_EGL_ON_X_BACKEND_H
