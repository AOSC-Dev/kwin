/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "item.h"

namespace KWin
{

class SurfacePixmap;

/**
 * The SurfaceItem class represents a surface with some contents.
 */
class KWIN_EXPORT SurfaceItem : public Item
{
    Q_OBJECT

public:
    QMatrix4x4 surfaceToBufferMatrix() const;
    void setSurfaceToBufferMatrix(const QMatrix4x4 &matrix);

    virtual QRegion shape() const;
    virtual QRegion opaque() const;

    void addDamage(const QRegion &region);
    void resetDamage();
    QRegion damage() const;

    void discardPixmap();
    void updatePixmap();

    SurfacePixmap *pixmap() const;
    SurfacePixmap *previousPixmap() const;

    void referencePreviousPixmap();
    void unreferencePreviousPixmap();

protected:
    explicit SurfaceItem(Scene::Window *window, Item *parent = nullptr);

    virtual SurfacePixmap *createPixmap() = 0;
    void preprocess() override;
    WindowQuadList buildQuads() const override;

    QRegion m_damage;
    QScopedPointer<SurfacePixmap> m_pixmap;
    QScopedPointer<SurfacePixmap> m_previousPixmap;
    QMatrix4x4 m_surfaceToBufferMatrix;
    int m_referencePixmapCounter = 0;
};

class KWIN_EXPORT SurfaceTextureProvider
{
public:
    virtual ~SurfaceTextureProvider();

    virtual bool isValid() const = 0;

    virtual bool create() = 0;
    virtual void update(const QRegion &region) = 0;
};

class KWIN_EXPORT SurfacePixmap : public QObject
{
    Q_OBJECT

public:
    explicit SurfacePixmap(SurfaceTextureProvider *platformTexture, QObject *parent = nullptr);

    SurfaceTextureProvider *textureProvider() const;

    bool hasAlphaChannel() const;
    QSize size() const;

    virtual void create() = 0;
    virtual void update();

    virtual bool isValid() const = 0;

protected:
    QSize m_size;
    bool m_hasAlphaChannel = false;

private:
    QScopedPointer<SurfaceTextureProvider> m_platformTexture;
};

} // namespace KWin
