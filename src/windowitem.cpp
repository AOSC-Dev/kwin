/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "windowitem.h"
#include "abstract_client.h"
#include "decorationitem.h"
#include "deleted.h"
#include "shadowitem.h"
#include "surfaceitem_internal.h"
#include "surfaceitem_wayland.h"
#include "surfaceitem_x11.h"

namespace KWin
{
WindowItem::WindowItem(Toplevel *window, Item *parent)
    : Item(parent)
    , m_window(window)
{
    AbstractClient *client = qobject_cast<AbstractClient *>(window);
    if (client) {
        connect(client, &AbstractClient::decorationChanged, this, &WindowItem::updateDecorationItem);
        updateDecorationItem();
    }
    connect(window, &Toplevel::windowClosed, this, &WindowItem::handleWindowClosed);
}

SurfaceItem *WindowItem::surfaceItem() const
{
    return m_surfaceItem.data();
}

DecorationItem *WindowItem::decorationItem() const
{
    return m_decorationItem.data();
}

ShadowItem *WindowItem::shadowItem() const
{
    return m_shadowItem.data();
}

Toplevel *WindowItem::window() const
{
    return m_window;
}

void WindowItem::handleWindowClosed(Toplevel *original, Deleted *deleted)
{
    Q_UNUSED(original)
    m_window = deleted;
}

void WindowItem::updateSurfaceItem(SurfaceItem *surfaceItem)
{
    m_surfaceItem.reset(surfaceItem);

    connect(m_window, &Toplevel::shadeChanged, this, &WindowItem::updateSurfaceVisibility);
    connect(m_window, &Toplevel::bufferGeometryChanged, this, &WindowItem::updateSurfacePosition);
    connect(m_window, &Toplevel::frameGeometryChanged, this, &WindowItem::updateSurfacePosition);

    updateSurfacePosition();
    updateSurfaceVisibility();
}

void WindowItem::updateSurfacePosition()
{
    const QRect bufferGeometry = m_window->bufferGeometry();
    const QRect frameGeometry = m_window->frameGeometry();

    m_surfaceItem->setPosition(bufferGeometry.topLeft() - frameGeometry.topLeft());
}

void WindowItem::updateSurfaceVisibility()
{
    m_surfaceItem->setVisible(!m_window->isShade());
}

void WindowItem::setShadow(Shadow *shadow)
{
    if (shadow) {
        if (!m_shadowItem || m_shadowItem->shadow() != shadow) {
            m_shadowItem.reset(new ShadowItem(shadow, m_window, this));
        }
        if (m_decorationItem) {
            m_shadowItem->stackBefore(m_decorationItem.data());
        } else if (m_surfaceItem) {
            m_shadowItem->stackBefore(m_decorationItem.data());
        }
    } else {
        m_shadowItem.reset();
    }
}

void WindowItem::updateDecorationItem()
{
    AbstractClient *client = qobject_cast<AbstractClient *>(m_window);
    if (!client || client->isZombie()) {
        return;
    }
    if (client->decoration()) {
        m_decorationItem.reset(new DecorationItem(client->decoration(), client, this));
        if (m_shadowItem) {
            m_decorationItem->stackAfter(m_shadowItem.data());
        } else if (m_surfaceItem) {
            m_decorationItem->stackBefore(m_surfaceItem.data());
        }
    } else {
        m_decorationItem.reset();
    }
}

WindowItemX11::WindowItemX11(Toplevel *window, Item *parent)
    : WindowItem(window, parent)
{
    switch (kwinApp()->operationMode()) {
    case Application::OperationModeX11:
        initialize();
        break;
    case Application::OperationModeXwayland:
        // Xwayland windows and Wayland surfaces are associated asynchronously.
        if (window->surface()) {
            initialize();
        } else {
            connect(window, &Toplevel::surfaceChanged, this, &WindowItemX11::initialize);
        }
        break;
    case Application::OperationModeWaylandOnly:
        Q_UNREACHABLE();
    }
}

void WindowItemX11::initialize()
{
    if (!window()->surface()) {
        updateSurfaceItem(new SurfaceItemX11(window(), this));
    } else {
        updateSurfaceItem(new SurfaceItemXwayland(window(), this));
    }
}

WindowItemWayland::WindowItemWayland(Toplevel *window, Item *parent)
    : WindowItem(window, parent)
{
    updateSurfaceItem(new SurfaceItemWayland(window->surface(), window, this));
}

WindowItemInternal::WindowItemInternal(Toplevel *window, Item *parent)
    : WindowItem(window, parent)
{
    updateSurfaceItem(new SurfaceItemInternal(window, this));
}

} // namespace KWin
