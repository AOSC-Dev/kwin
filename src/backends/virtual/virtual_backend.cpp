/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "virtual_backend.h"

#include "composite.h"
#include "virtual_egl_backend.h"
#include "virtual_output.h"
#include "virtual_qpainter_backend.h"
// Qt
#include <QTemporaryDir>

namespace KWin
{

VirtualBackend::VirtualBackend(QObject *parent)
    : Platform(parent)
{
    if (qEnvironmentVariableIsSet("KWIN_WAYLAND_VIRTUAL_SCREENSHOTS")) {
        m_screenshotDir.reset(new QTemporaryDir);
        if (!m_screenshotDir->isValid()) {
            m_screenshotDir.reset();
        }
        if (m_screenshotDir) {
            qDebug() << "Screenshots saved to: " << m_screenshotDir->path();
        }
    }

    setSupportsPointerWarping(true);
    setSupportsGammaControl(true);
}

VirtualBackend::~VirtualBackend()
{
    if (sceneEglDisplay() != EGL_NO_DISPLAY) {
        eglTerminate(sceneEglDisplay());
    }
}

bool VirtualBackend::initialize()
{
    /*
     * Some tests currently expect one output present at start,
     * others set them explicitly.
     *
     * TODO: rewrite all tests to explicitly set the outputs.
     */
    if (m_outputs.isEmpty()) {
        auto dummyOutput = std::make_shared<VirtualOutput>(this);
        dummyOutput->init(QPoint(0, 0), initialWindowSize());
        m_outputs << dummyOutput;
        Q_EMIT outputAdded(dummyOutput);
        dummyOutput->updateEnabled(true);
    }
    setReady(true);

    Q_EMIT screensQueried();
    return true;
}

QString VirtualBackend::screenshotDirPath() const
{
    if (!m_screenshotDir) {
        return QString();
    }
    return m_screenshotDir->path();
}

std::unique_ptr<QPainterBackend> VirtualBackend::createQPainterBackend()
{
    return std::make_unique<VirtualQPainterBackend>(this);
}

std::unique_ptr<OpenGLBackend> VirtualBackend::createOpenGLBackend()
{
    return std::make_unique<VirtualEglBackend>(this);
}

Outputs VirtualBackend::outputs() const
{
    return m_outputs;
}

void VirtualBackend::setVirtualOutputs(int count, QVector<QRect> geometries, QVector<int> scales)
{
    Q_ASSERT(geometries.size() == 0 || geometries.size() == count);
    Q_ASSERT(scales.size() == 0 || scales.size() == count);

    const QVector<std::shared_ptr<VirtualOutput>> removed = m_outputs;

    int sumWidth = 0;
    for (int i = 0; i < count; i++) {
        auto vo = std::make_shared<VirtualOutput>(this);
        if (geometries.size()) {
            const QRect geo = geometries.at(i);
            vo->init(geo.topLeft(), geo.size());
        } else {
            vo->init(QPoint(sumWidth, 0), initialWindowSize());
            sumWidth += initialWindowSize().width();
        }
        if (scales.size()) {
            vo->updateScale(scales.at(i));
        }
        m_outputs.append(vo);
        Q_EMIT outputAdded(vo);
        vo->updateEnabled(true);
    }

    for (const std::shared_ptr<VirtualOutput> &output : removed) {
        output->updateEnabled(false);
        m_outputs.removeOne(output);
        Q_EMIT outputRemoved(output);
    }

    Q_EMIT screensQueried();
}

QImage VirtualBackend::captureOutput(Output *output) const
{
    if (auto backend = qobject_cast<VirtualQPainterBackend *>(Compositor::self()->backend())) {
        if (auto layer = backend->primaryLayer(output)) {
            return *layer->image();
        }
    }
    return QImage();
}

} // namespace KWin
