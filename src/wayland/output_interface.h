/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
#pragma once

#include "kwin_export.h"
#include "output.h"

#include <QObject>
#include <QPoint>
#include <QSize>

struct wl_resource;
struct wl_client;

namespace KWaylandServer
{
class ClientConnection;
class Display;
class OutputInterfacePrivate;

/**
 * The OutputInterface class represents a screen. This class corresponds to the Wayland
 * interface @c wl_output.
 */
class KWIN_EXPORT OutputInterface : public QObject
{
    Q_OBJECT
public:
    explicit OutputInterface(Display *display, KWin::Output *output);
    ~OutputInterface() override;

    void remove();

    /**
     * @returns all wl_resources bound for the @p client
     */
    QVector<wl_resource *> clientResources(ClientConnection *client) const;

    /**
     * Submit changes to all clients.
     */
    void done();

    /**
     * Submit changes to @p client.
     */
    void done(wl_client *client);

    KWin::Output *output() const;

    void update();

    static OutputInterface *get(wl_resource *native);

Q_SIGNALS:
    void removed();

    /**
     * Emitted when a client binds to a given output
     * @internal
     */
    void bound(ClientConnection *client, wl_resource *boundResource);

private:
    QScopedPointer<OutputInterfacePrivate> d;
};

} // namespace KWaylandServer
