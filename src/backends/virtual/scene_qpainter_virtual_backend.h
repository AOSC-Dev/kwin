/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_SCENE_QPAINTER_VIRTUAL_BACKEND_H
#define KWIN_SCENE_QPAINTER_VIRTUAL_BACKEND_H

#include "qpainterbackend.h"

#include "outputlayer.h"

#include <QMap>
#include <QObject>
#include <QVector>

namespace KWin
{

class VirtualBackend;

class VirtualQPainterLayer : public OutputLayer
{
public:
    VirtualQPainterLayer(AbstractOutput *output);

    QRegion beginFrame() override;
    void endFrame(const QRegion &renderedRegion, const QRegion &damagedRegion) override;
    QImage *image() override;

private:
    AbstractOutput *const m_output;
    QImage m_image;
};

class VirtualQPainterBackend : public QPainterBackend
{
    Q_OBJECT
public:
    VirtualQPainterBackend(VirtualBackend *backend);
    ~VirtualQPainterBackend() override;

    void present(AbstractOutput *output) override;
    OutputLayer *getLayer(RenderOutput *output) override;

private:
    void createOutputs();

    QMap<AbstractOutput *, QSharedPointer<VirtualQPainterLayer>> m_backBuffers;
    VirtualBackend *m_backend;
    int m_frameCounter = 0;
};

}

#endif
