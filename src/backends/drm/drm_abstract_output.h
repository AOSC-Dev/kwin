/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2021 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "output.h"

namespace KWin
{

class DrmBackend;
class DrmGpu;
class DrmOutputLayer;
class RenderOutput;
class SimpleRenderOutput;

class DrmAbstractOutput : public Output
{
    Q_OBJECT
public:
    DrmAbstractOutput(DrmGpu *gpu);
    ~DrmAbstractOutput();

    RenderLoop *renderLoop() const override;
    void frameFailed() const;
    void pageFlipped(std::chrono::nanoseconds timestamp) const;
    QVector<int32_t> regionToRects(const QRegion &region) const;
    DrmGpu *gpu() const;

    virtual bool present() = 0;
    virtual DrmOutputLayer *outputLayer() const = 0;
    RenderOutput *renderOutput() const;

protected:
    friend class DrmGpu;

    QScopedPointer<SimpleRenderOutput> m_renderOutput;
    RenderLoop *m_renderLoop;
    DrmGpu *const m_gpu;
};

}
