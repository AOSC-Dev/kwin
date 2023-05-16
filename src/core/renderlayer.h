/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwin_export.h"

#include "libkwineffects/regionf.h"
#include "outputlayer.h"

#include <QMap>
#include <QObject>
#include <QPointer>

namespace KWin
{

class OutputLayer;
class RenderLayerDelegate;
class RenderLoop;

/**
 * The RenderLayer class represents a composited layer.
 *
 * The contents of the layer is represented by the RenderLayerDelegate. The render layer
 * takes the ownership of the delegate.
 *
 * Each render layer has a hardware layer assigned to it, represented by OutputLayer. If
 * the output layer is not set explicitly, it's inherited from the parent render layer.
 * Output layers can be freely assigned or unassigned to render layers.
 */
class KWIN_EXPORT RenderLayer : public QObject
{
    Q_OBJECT

public:
    explicit RenderLayer(RenderLoop *loop, RenderLayer *superlayer = nullptr);
    ~RenderLayer() override;

    OutputLayer *outputLayer() const;
    void setOutputLayer(OutputLayer *layer);

    RenderLoop *loop() const;
    RenderLayerDelegate *delegate() const;
    void setDelegate(std::unique_ptr<RenderLayerDelegate> delegate);

    QList<RenderLayer *> sublayers() const;
    RenderLayer *superlayer() const;
    void setSuperlayer(RenderLayer *layer);

    bool isVisible() const;
    void setVisible(bool visible);

    QPoint mapToGlobal(const QPoint &point) const;
    QPointF mapToGlobal(const QPointF &point) const;
    QRegion mapToGlobal(const QRegion &region) const;
    RegionF mapToGlobal(const RegionF &region) const;
    QRect mapToGlobal(const QRect &rect) const;
    QRectF mapToGlobal(const QRectF &rect) const;

    QPoint mapFromGlobal(const QPoint &point) const;
    QPointF mapFromGlobal(const QPointF &point) const;
    QRegion mapFromGlobal(const QRegion &region) const;
    QRect mapFromGlobal(const QRect &rect) const;
    QRectF mapFromGlobal(const QRectF &rect) const;

    QRectF rect() const;
    QRectF boundingRect() const;

    QRectF geometry() const;
    void setGeometry(const QRectF &rect);

    void addRepaint(const RegionF &region);
    void addRepaint(const QRectF &rect);
    void addRepaint(double x, double y, double width, double height);
    void addRepaintFull();
    RegionF repaints() const;
    void resetRepaints();

private:
    void addSublayer(RenderLayer *sublayer);
    void removeSublayer(RenderLayer *sublayer);
    void updateBoundingRect();
    void updateEffectiveVisibility();
    bool computeEffectiveVisibility() const;

    RenderLoop *m_loop;
    std::unique_ptr<RenderLayerDelegate> m_delegate;
    RegionF m_repaints;
    QRectF m_boundingRect;
    QRectF m_geometry;
    QPointer<OutputLayer> m_outputLayer;
    RenderLayer *m_superlayer = nullptr;
    QList<RenderLayer *> m_sublayers;
    bool m_effectiveVisible = true;
    bool m_explicitVisible = true;
};

} // namespace KWin
