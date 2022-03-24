/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2019 Roman Gilg <subdiff@gmail.com>
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#define WL_EGL_PLATFORM 1

#include "egl_wayland_backend.h"
#include "basiceglsurfacetexture_internal.h"
#include "basiceglsurfacetexture_wayland.h"

#include "wayland_backend.h"
#include "wayland_output.h"

#include "composite.h"
#include "kwinglutils.h"
#include "logging.h"
#include "options.h"

#include "renderoutput.h"
#include "screens.h"
#include "wayland_server.h"

#include <fcntl.h>
#include <unistd.h>

// kwin libs
#include <kwinglplatform.h>

// KDE
#include <KWayland/Client/surface.h>

// Qt
#include <QFile>
#include <QOpenGLContext>

#include <cmath>

namespace KWin
{
namespace Wayland
{

static QVector<EGLint> regionToRects(const QRegion &region, AbstractWaylandOutput *output)
{
    const int height = output->modeSize().height();
    const QMatrix4x4 matrix = WaylandOutput::logicalToNativeMatrix(output->geometry(),
                                                                   output->scale(),
                                                                   output->transform());

    QVector<EGLint> rects;
    rects.reserve(region.rectCount() * 4);
    for (const QRect &_rect : region) {
        const QRect rect = matrix.mapRect(_rect);

        rects << rect.left();
        rects << height - (rect.y() + rect.height());
        rects << rect.width();
        rects << rect.height();
    }
    return rects;
}

EglWaylandOutput::EglWaylandOutput(WaylandOutput *output, EglWaylandBackend *backend)
    : m_backend(backend)
    , m_waylandOutput(output)
{
}

EglWaylandOutput::~EglWaylandOutput()
{
    wl_egl_window_destroy(m_overlay);
}

bool EglWaylandOutput::init(EglWaylandBackend *backend)
{
    auto surface = m_waylandOutput->surface();
    const QSize nativeSize = m_waylandOutput->geometry().size() * m_waylandOutput->scale();
    auto overlay = wl_egl_window_create(*surface, nativeSize.width(), nativeSize.height());
    if (!overlay) {
        qCCritical(KWIN_WAYLAND_BACKEND) << "Creating Wayland Egl window failed";
        return false;
    }
    m_overlay = overlay;
    m_renderTarget.reset(new GLRenderTarget(0, nativeSize));

    EGLSurface eglSurface = EGL_NO_SURFACE;
    if (backend->havePlatformBase()) {
        eglSurface = eglCreatePlatformWindowSurfaceEXT(backend->eglDisplay(), backend->config(), (void *)overlay, nullptr);
    } else {
        eglSurface = eglCreateWindowSurface(backend->eglDisplay(), backend->config(), overlay, nullptr);
    }
    if (eglSurface == EGL_NO_SURFACE) {
        qCCritical(KWIN_WAYLAND_BACKEND) << "Create Window Surface failed";
        return false;
    }
    m_eglSurface = eglSurface;

    connect(m_waylandOutput, &WaylandOutput::sizeChanged, this, &EglWaylandOutput::updateSize);
    connect(m_waylandOutput, &WaylandOutput::currentModeChanged, this, &EglWaylandOutput::updateSize);
    connect(m_waylandOutput, &WaylandOutput::geometryChanged, this, &EglWaylandOutput::resetBufferAge);

    return true;
}

void EglWaylandOutput::updateSize()
{
    const QSize nativeSize = m_waylandOutput->geometry().size() * m_waylandOutput->scale();
    m_renderTarget.reset(new GLRenderTarget(0, nativeSize));

    wl_egl_window_resize(m_overlay, nativeSize.width(), nativeSize.height(), 0, 0);
    resetBufferAge();
}

void EglWaylandOutput::resetBufferAge()
{
    m_bufferAge = 0;
}

bool EglWaylandOutput::makeContextCurrent()
{
    const EGLSurface eglSurface = m_eglSurface;
    if (eglSurface == EGL_NO_SURFACE) {
        return false;
    }
    if (eglMakeCurrent(m_backend->eglDisplay(), eglSurface, eglSurface, m_backend->context()) == EGL_FALSE) {
        qCCritical(KWIN_WAYLAND_BACKEND) << "Make Context Current failed";
        return false;
    }

    EGLint error = eglGetError();
    if (error != EGL_SUCCESS) {
        qCWarning(KWIN_WAYLAND_BACKEND) << "Error occurred while creating context " << error;
        return false;
    }

    return true;
}

QRegion EglWaylandOutput::beginFrame()
{
    eglWaitNative(EGL_CORE_NATIVE_ENGINE);
    makeContextCurrent();
    GLRenderTarget::pushRenderTarget(m_renderTarget.get());

    if (m_backend->supportsBufferAge()) {
        return m_damageJournal.accumulate(m_bufferAge, m_waylandOutput->geometry());
    }
    return QRegion();
}

void EglWaylandOutput::endFrame(const QRegion &renderedRegion, const QRegion &damagedRegion)
{
}

void EglWaylandOutput::aboutToStartPainting(const QRegion &damage)
{
    if (m_bufferAge > 0 && !damage.isEmpty() && m_backend->supportsPartialUpdate()) {
        QVector<EGLint> rects = regionToRects(damage, m_waylandOutput);
        const bool correct = eglSetDamageRegionKHR(m_backend->eglDisplay(), m_eglSurface,
                                                   rects.data(), rects.count() / 4);
        if (!correct) {
            qCWarning(KWIN_WAYLAND_BACKEND) << "failed eglSetDamageRegionKHR" << eglGetError();
        }
    }
}

EglWaylandBackend::EglWaylandBackend(WaylandBackend *b)
    : AbstractEglBackend()
    , m_backend(b)
{
    if (!m_backend) {
        setFailed("Wayland Backend has not been created");
        return;
    }
    qCDebug(KWIN_WAYLAND_BACKEND) << "Connected to Wayland display?" << (m_backend->display() ? "yes" : "no");
    if (!m_backend->display()) {
        setFailed("Could not connect to Wayland compositor");
        return;
    }

    // Egl is always direct rendering
    setIsDirectRendering(true);

    connect(m_backend, &WaylandBackend::outputAdded, this, &EglWaylandBackend::createEglWaylandOutput);
    connect(m_backend, &WaylandBackend::outputRemoved, this, [this](AbstractOutput *output) {
        auto it = std::find_if(m_outputs.begin(), m_outputs.end(), [output](const auto &o) {
            return o->m_waylandOutput == output;
        });
        if (it == m_outputs.end()) {
            return;
        }
        m_outputs.erase(it);
    });
}

EglWaylandBackend::~EglWaylandBackend()
{
    cleanup();
}

void EglWaylandBackend::cleanupSurfaces()
{
    m_outputs.clear();
}

bool EglWaylandBackend::createEglWaylandOutput(AbstractOutput *waylandOutput)
{
    const auto &output = QSharedPointer<EglWaylandOutput>::create(static_cast<WaylandOutput *>(waylandOutput), this);
    if (!output->init(this)) {
        return false;
    }
    m_outputs.insert(waylandOutput, output);
    return true;
}

bool EglWaylandBackend::initializeEgl()
{
    initClientExtensions();
    EGLDisplay display = m_backend->sceneEglDisplay();

    // Use eglGetPlatformDisplayEXT() to get the display pointer
    // if the implementation supports it.
    if (display == EGL_NO_DISPLAY) {
        m_havePlatformBase = hasClientExtension(QByteArrayLiteral("EGL_EXT_platform_base"));
        if (m_havePlatformBase) {
            // Make sure that the wayland platform is supported
            if (!hasClientExtension(QByteArrayLiteral("EGL_EXT_platform_wayland"))) {
                return false;
            }

            display = eglGetPlatformDisplayEXT(EGL_PLATFORM_WAYLAND_EXT, m_backend->display(), nullptr);
        } else {
            display = eglGetDisplay(m_backend->display());
        }
    }

    if (display == EGL_NO_DISPLAY) {
        return false;
    }
    setEglDisplay(display);
    return initEglAPI();
}

void EglWaylandBackend::init()
{
    if (!initializeEgl()) {
        setFailed("Could not initialize egl");
        return;
    }
    if (!initRenderingContext()) {
        setFailed("Could not initialize rendering context");
        return;
    }

    initKWinGL();
    initBufferAge();
    initWayland();
}

bool EglWaylandBackend::initRenderingContext()
{
    initBufferConfigs();

    if (!createContext()) {
        return false;
    }

    auto waylandOutputs = m_backend->waylandOutputs();

    // we only allow to start with at least one output
    if (waylandOutputs.isEmpty()) {
        return false;
    }

    for (auto *out : waylandOutputs) {
        if (!createEglWaylandOutput(out)) {
            return false;
        }
    }

    if (m_outputs.isEmpty()) {
        qCCritical(KWIN_WAYLAND_BACKEND) << "Create Window Surfaces failed";
        return false;
    }

    const auto firstOutput = m_outputs.first();
    // set our first surface as the one for the abstract backend, just to make it happy
    setSurface(firstOutput->m_eglSurface);
    return firstOutput->makeContextCurrent();
}

bool EglWaylandBackend::initBufferConfigs()
{
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,
        EGL_WINDOW_BIT,
        EGL_RED_SIZE,
        1,
        EGL_GREEN_SIZE,
        1,
        EGL_BLUE_SIZE,
        1,
        EGL_ALPHA_SIZE,
        0,
        EGL_RENDERABLE_TYPE,
        isOpenGLES() ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT,
        EGL_CONFIG_CAVEAT,
        EGL_NONE,
        EGL_NONE,
    };

    EGLint count;
    EGLConfig configs[1024];
    if (eglChooseConfig(eglDisplay(), config_attribs, configs, 1, &count) == EGL_FALSE) {
        qCCritical(KWIN_WAYLAND_BACKEND) << "choose config failed";
        return false;
    }
    if (count != 1) {
        qCCritical(KWIN_WAYLAND_BACKEND) << "choose config did not return a config" << count;
        return false;
    }
    setConfig(configs[0]);

    return true;
}


QSharedPointer<GLTexture> EglWaylandBackend::textureForOutput(RenderOutput *output) const
{
    QSharedPointer<GLTexture> texture(new GLTexture(GL_RGBA8, output->pixelSize()));
    GLRenderTarget::pushRenderTarget(m_outputs[output->platformOutput()]->m_renderTarget.get());
    GLRenderTarget renderTarget(texture.data());
    renderTarget.blitFromFramebuffer(QRect(0, texture->height(), texture->width(), -texture->height()));
    GLRenderTarget::popRenderTarget();
    return texture;
}

void EglWaylandBackend::presentOnSurface(EglWaylandOutput *output, const QRegion &damage)
{
    WaylandOutput *waylandOutput = output->m_waylandOutput;

    waylandOutput->surface()->setupFrameCallback();
    waylandOutput->surface()->setScale(std::ceil(waylandOutput->scale()));
    Q_EMIT waylandOutput->outputChange(damage);

    if (supportsSwapBuffersWithDamage()) {
        QVector<EGLint> rects = regionToRects(damage, waylandOutput);
        if (!eglSwapBuffersWithDamageEXT(eglDisplay(), output->m_eglSurface,
                                         rects.data(), rects.count() / 4)) {
            qCCritical(KWIN_WAYLAND_BACKEND, "eglSwapBuffersWithDamage() failed: %x", eglGetError());
        }
    } else {
        if (!eglSwapBuffers(eglDisplay(), output->m_eglSurface)) {
            qCCritical(KWIN_WAYLAND_BACKEND, "eglSwapBuffers() failed: %x", eglGetError());
        }
    }

    if (supportsBufferAge()) {
        eglQuerySurface(eglDisplay(), output->m_eglSurface, EGL_BUFFER_AGE_EXT, &output->m_bufferAge);
    }
}

SurfaceTexture *EglWaylandBackend::createSurfaceTextureInternal(SurfacePixmapInternal *pixmap)
{
    return new BasicEGLSurfaceTextureInternal(this, pixmap);
}

SurfaceTexture *EglWaylandBackend::createSurfaceTextureWayland(SurfacePixmapWayland *pixmap)
{
    return new BasicEGLSurfaceTextureWayland(this, pixmap);
}

void EglWaylandBackend::present(AbstractOutput *output)
{
    Q_ASSERT(m_outputs.contains(output));
    GLRenderTarget::popRenderTarget();

    const auto &eglOutput = m_outputs[output];
    presentOnSurface(eglOutput.get(), m_lastDamage);

    if (supportsBufferAge()) {
        eglOutput->m_damageJournal.add(m_lastDamage);
    }
}

OutputLayer *EglWaylandBackend::getLayer(RenderOutput *output)
{
    return m_outputs[output->platformOutput()].get();
}
}
}
