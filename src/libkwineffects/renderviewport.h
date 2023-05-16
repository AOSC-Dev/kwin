/*
    SPDX-FileCopyrightText: 2023 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "libkwineffects/kwineffects_export.h"
#include "libkwineffects/regionf.h"

#include <QMatrix4x4>
#include <QRectF>
#include <QRegion>

namespace KWin
{

class RenderTarget;

class KWINEFFECTS_EXPORT RenderViewport
{
public:
    explicit RenderViewport(const QRectF &renderRect, double scale, const RenderTarget &renderTarget);

    QMatrix4x4 projectionMatrix() const;
    QRectF renderRect() const;
    double scale() const;

    QRectF mapToRenderTarget(const QRectF &logicalGeometry) const;
    QRect mapToRenderTarget(const QRect &logicalGeometry) const;
    QPoint mapToRenderTarget(const QPoint &logicalGeometry) const;
    QPointF mapToRenderTarget(const QPointF &logicalGeometry) const;
    QRegion mapToRenderTarget(const QRegion &logicalGeometry) const;
    RegionF mapToRenderTarget(const RegionF &logicalGeometry) const;

    QRectF mapToRenderTargetTexture(const QRectF &logicalGeometry) const;
    QRect mapToRenderTargetTexture(const QRect &logicalGeometry) const;
    QPoint mapToRenderTargetTexture(const QPoint &logicalGeometry) const;
    QPointF mapToRenderTargetTexture(const QPointF &logicalGeometry) const;
    QRegion mapToRenderTargetTexture(const QRegion &logicalGeometry) const;

private:
    const QRectF m_renderRect;
    const QMatrix4x4 m_projectionMatrix;
    const QMatrix4x4 m_logicalToLocal;
    const QMatrix4x4 m_logicalToLocalTexture;
    const double m_scale;
};

} // namespace KWin
