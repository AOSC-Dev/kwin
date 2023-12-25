#include "markerinterface.h"
#include "elfparser.h"

#include <qobject.h>
#include <qthread.h>
#include <thread>
#include <unistd.h>

#include "atoms.h"
#include "window.h"
#include "workspace.h"
#include "x11window.h"
#include <KLocalizedString>

namespace KWin
{
static inline void appendMarker(const xcb_window_t xcb_window, const QString &caption)
{
    const QString markerName = i18n("(Compatibility Mode)");
    if (caption.endsWith(markerName)) {
        return;
    }
    xcb_connection_t *xcb_connection = connection();
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(xcb_connection, true, strlen("_NET_WM_NAME"), "_NET_WM_NAME");
    QString newTitle = QStringLiteral("%1 %2").arg(caption).arg(markerName);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(xcb_connection, cookie, nullptr);
    if (reply) {
        const auto newTitleData = newTitle.toUtf8();
        xcb_change_property(xcb_connection, XCB_PROP_MODE_REPLACE, xcb_window, reply->atom, atoms->utf8_string, 8, newTitleData.size(), newTitleData.constData());
        free(reply);
    }
}

CompatMarkerPlugin::CompatMarkerPlugin()
{
    QThread *initialScanner = QThread::create([&] {
        scanAll();
    });
    connect(workspace(), &Workspace::windowAdded, this, &CompatMarkerPlugin::scanOne);
    // collect the thread object
    connect(initialScanner, &QThread::finished, initialScanner, &QObject::deleteLater);
    initialScanner->start();
}

bool CompatMarkerPlugin::scanOne(const Window *window)
{
    auto pid = window->pid();
    if (!pid || pid == getpid())
        return false;
    std::string exe_path = "/proc/" + std::to_string(pid) + "/exe";
    bool result = is_old_binary(exe_path.c_str());
    if (result) {
        appendMarker(window->window(), window->captionNormal());
        // re-mark the window when the application changes its caption
        connect(window, &Window::captionChanged, [window] {
            appendMarker(window->window(), window->captionNormal());
        });
    }
    return result;
}

void CompatMarkerPlugin::scanAll()
{
    // currently only supports X11Window
    for (const X11Window *window : Workspace::self()->clientList()) {
        scanOne(window);
    }
}
}

#include "moc_markerinterface.cpp"
