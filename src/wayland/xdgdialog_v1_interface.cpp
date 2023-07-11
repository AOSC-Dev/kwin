/*
    SPDX-FileCopyrightText: 2023 David Redondo <kde@david-redondo.de>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/

#include "xdgdialog_v1_interface.h"

#include "display.h"
#include "xdgshell_interface.h"

#include <qwayland-server-dialog-v1.h>

#include <QHash>

namespace KWaylandServer
{

constexpr int version = 1;

class XdgDialogWmV1InterfacePrivate : public QtWaylandServer::xdg_wm_dialog_v1
{
public:
    XdgDialogWmV1InterfacePrivate(Display *display, XdgDialogWmV1Interface *q);

    XdgDialogWmV1Interface *const q;
    QHash<XdgDialogV1Interface *, Resource *> m_dialogs;

protected:
    void xdg_wm_dialog_v1_get_xdg_dialog(Resource *resource, uint32_t id, wl_resource *toplevel) override;
    void xdg_wm_dialog_v1_destroy(Resource *resource) override;
};

class XdgDialogV1InterfacePrivate : public QtWaylandServer::xdg_dialog_v1
{
public:
    XdgDialogV1InterfacePrivate(wl_resource *resource, XdgDialogV1Interface *q);

    XdgDialogV1Interface *const q;
    XdgDialogWmV1InterfacePrivate *m_wm;
    XdgToplevelInterface *m_toplevel;
    bool modal = false;

protected:
    void xdg_dialog_v1_destroy_resource(Resource *resource) override;
    void xdg_dialog_v1_set_modal(Resource *resource) override;
    void xdg_dialog_v1_unset_modal(Resource *resource) override;
};

XdgDialogWmV1InterfacePrivate::XdgDialogWmV1InterfacePrivate(Display *display, XdgDialogWmV1Interface *q)
    : xdg_wm_dialog_v1(*display, version)
    , q(q)
{
}

void XdgDialogWmV1InterfacePrivate::xdg_wm_dialog_v1_destroy(Resource *resource)
{
    // if (m_dialogs.key(resource)) {
    //     wl_resource_post_error(resource->handle, error_defunct_dialogs, "xdg_wm_dialog_v1 destroyed before children");
    //     return;
    // }
    wl_resource_destroy(resource->handle);
}

void XdgDialogWmV1InterfacePrivate::xdg_wm_dialog_v1_get_xdg_dialog(Resource *resource, uint32_t id, wl_resource *toplevel)
{
    auto toplevelInterface = XdgToplevelInterface::get(toplevel);
    if (!toplevelInterface) {
        wl_resource_post_error(resource->handle, 0, "Invalid surface");
        return;
    }
    auto dialogResource = wl_resource_create(resource->client(), &xdg_dialog_v1_interface, resource->version(), id);
    auto dialog = new XdgDialogV1Interface(dialogResource);
    dialog->d->m_wm = this;
    dialog->d->m_toplevel = toplevelInterface;
    m_dialogs.insert(dialog, resource);
    Q_EMIT q->dialogCreated(dialog);
}

XdgDialogWmV1Interface::XdgDialogWmV1Interface(Display *display, QObject *parent)
    : d(std::make_unique<XdgDialogWmV1InterfacePrivate>(display, this))
{
}

XdgDialogWmV1Interface::~XdgDialogWmV1Interface() = default;

XdgDialogV1Interface *XdgDialogWmV1Interface::dialogForToplevel(XdgToplevelInterface *toplevel) const
{
    auto it = std::find_if(d->m_dialogs.keyBegin(), d->m_dialogs.keyEnd(), [toplevel](XdgDialogV1Interface *dialog) {
        return dialog->toplevel() == toplevel;
    });
    return it == d->m_dialogs.keyEnd() ? nullptr : *it;
}

XdgDialogV1InterfacePrivate::XdgDialogV1InterfacePrivate(wl_resource *wl_resource, XdgDialogV1Interface *q)
    : xdg_dialog_v1(wl_resource)
    , q(q)
{
}

void XdgDialogV1InterfacePrivate::xdg_dialog_v1_destroy_resource(Resource *resource)
{
    delete q;
}

void XdgDialogV1InterfacePrivate::xdg_dialog_v1_set_modal(Resource *resource)
{
    modal = true;
    Q_EMIT q->modalChanged(true);
}

void XdgDialogV1InterfacePrivate::xdg_dialog_v1_unset_modal(Resource *resource)
{
    modal = false;
    Q_EMIT q->modalChanged(false);
}

XdgDialogV1Interface::XdgDialogV1Interface(wl_resource *resource)
    : d(std::make_unique<XdgDialogV1InterfacePrivate>(resource, this))
{
}

XdgDialogV1Interface::~XdgDialogV1Interface()
{
    d->m_wm->m_dialogs.remove(this);
}

bool XdgDialogV1Interface::isModal() const
{
    return d->modal;
}

XdgToplevelInterface *XdgDialogV1Interface::toplevel() const
{
    return d->m_toplevel;
}
}
