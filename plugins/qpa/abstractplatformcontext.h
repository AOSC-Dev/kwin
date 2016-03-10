/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>

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
#ifndef KWIN_QPA_ABSTRACTPLATFORMCONTEXT_H
#define KWIN_QPA_ABSTRACTPLATFORMCONTEXT_H

#include <epoxy/egl.h>
#include <fixx11h.h>
#include <qpa/qplatformopenglcontext.h>

namespace KWin
{
namespace QPA
{
class Integration;

class AbstractPlatformContext : public QPlatformOpenGLContext
{
public:
    explicit AbstractPlatformContext(QOpenGLContext *context, Integration *integration, EGLDisplay display);
    virtual ~AbstractPlatformContext();

    void doneCurrent() override;
    QSurfaceFormat format() const override;
    bool isValid() const override;
#if (QT_VERSION < QT_VERSION_CHECK(5, 7, 0))
    QFunctionPointer getProcAddress(const QByteArray &procName) override;
#else
    QFunctionPointer getProcAddress(const char *procName) override;
#endif

protected:
    EGLDisplay eglDisplay() const {
        return m_eglDisplay;
    }
    EGLConfig config() const {
        return m_config;
    }
    bool bindApi();
    EGLContext context() const {
        return m_context;
    }
    void createContext(EGLContext shareContext = EGL_NO_CONTEXT);

private:
    Integration *m_integration;
    EGLDisplay m_eglDisplay;
    EGLConfig m_config;
    EGLContext m_context = EGL_NO_CONTEXT;
    QSurfaceFormat m_format;
};

}
}

#endif
