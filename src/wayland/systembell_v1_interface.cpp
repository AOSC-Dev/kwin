/*
    SPDX-FileCopyrightText: 2023 David Redondo <kde@david-redondo.de>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "systembell_v1_interface.h"

#include "clientconnection.h"
#include "display.h"
#include "surface_interface.h"

#include <qwayland-server-system-bell-v1.h>

namespace KWaylandServer
{

constexpr int version = 1;

class SystemBellV1InterfacePrivate : public QtWaylandServer::wp_system_bell_v1
{
public:
    SystemBellV1InterfacePrivate(SystemBellV1Interface *q, Display *display);

    SystemBellV1Interface *q;
    Display *display;

protected:
    void wp_system_bell_v1_ring(Resource *resource, wl_resource *surface) override;
    void wp_system_bell_v1_destroy(Resource *resource) override;
};

SystemBellV1InterfacePrivate::SystemBellV1InterfacePrivate(SystemBellV1Interface *q, Display *display)
    : QtWaylandServer::wp_system_bell_v1(*display, version)
    , q(q)
    , display(display)
{
}

void SystemBellV1InterfacePrivate::wp_system_bell_v1_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void SystemBellV1InterfacePrivate::wp_system_bell_v1_ring(Resource *resource, wl_resource *surface)
{
    if (surface) {
        if (auto surfaceInterface = SurfaceInterface::get(surface)) {
            Q_EMIT q->ringSurface(surfaceInterface);
        } else {
            wl_resource_post_error(resource->handle, 0, "Invalid surface");
        }
    }
    Q_EMIT q->ring(display->getConnection(resource->client()));
}

SystemBellV1Interface::SystemBellV1Interface(KWaylandServer::Display *display, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<SystemBellV1InterfacePrivate>(this, display))
{
}

SystemBellV1Interface::~SystemBellV1Interface() = default;

}
