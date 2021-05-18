/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "clientbufferref.h"
#include "surfaceitem.h"

namespace KWin
{

/**
 * The SurfaceItemInternal class represents an internal surface in the scene.
 */
class KWIN_EXPORT SurfaceItemInternal : public SurfaceItem
{
    Q_OBJECT

public:
    explicit SurfaceItemInternal(Scene::Window *window, Item *parent = nullptr);

    QPointF mapToBuffer(const QPointF &point) const override;
    QRegion shape() const override;

private Q_SLOTS:
    void handleBufferGeometryChanged(Toplevel *toplevel, const QRect &old);

protected:
    SurfacePixmap *createPixmap() override;
};

class KWIN_EXPORT SurfacePixmapInternal final : public SurfacePixmap
{
    Q_OBJECT

public:
    explicit SurfacePixmapInternal(SurfaceItemInternal *item, QObject *parent = nullptr);

    ClientBufferRef buffer() const;

    void create() override;
    void update() override;
    bool isValid() const override;

private:
    SurfaceItemInternal *m_item;
    ClientBufferRef m_bufferRef;
};

} // namespace KWin
