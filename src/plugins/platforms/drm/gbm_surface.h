/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2017 Martin Flöser <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_DRM_GBM_SURFACE_H
#define KWIN_DRM_GBM_SURFACE_H

#include <cstdint>
#include <epoxy/egl.h>
#include <QVector>

#include "drm_buffer_gbm.h"

struct gbm_device;
struct gbm_surface;

namespace KWin
{

class GbmSurface
{
public:
    explicit GbmSurface(DrmGpu *gpu, const QSize &size, uint32_t format, uint32_t flags);
    ~GbmSurface();

    QSharedPointer<DrmGbmBuffer> swapBuffersForDrm();
    QSharedPointer<GbmBuffer> swapBuffers();

    void releaseBuffer(GbmBuffer *buffer);

    QSharedPointer<GbmBuffer> currentBuffer() const;
    QSharedPointer<DrmGbmBuffer> currentDrmBuffer() const;

    EGLSurface eglSurface() const {
        return m_eglSurface;
    }

    bool isValid() const {
        return m_surface != nullptr && m_eglSurface != EGL_NO_SURFACE;
    }

private:
    gbm_surface *m_surface;
    DrmGpu *m_gpu;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;

    QSharedPointer<GbmBuffer> m_currentBuffer;
    QSharedPointer<DrmGbmBuffer> m_currentDrmBuffer;
    QVector<GbmBuffer*> m_lockedBuffers;
};

}

#endif
