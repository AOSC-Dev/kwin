/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2006 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2010, 2011 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "effects.h"

#include <config-kwin.h>

#include "effectloader.h"
#include "effectsadaptor.h"
#include "output.h"
#if KWIN_BUILD_ACTIVITIES
#include "activities.h"
#endif
#include "cursor.h"
#include "group.h"
#include "input_event.h"
#include "internalwindow.h"
#include "osd.h"
#include "pointer_input.h"
#include "renderbackend.h"
#include "renderlayer.h"
#include "unmanaged.h"
#include "x11window.h"
#if KWIN_BUILD_TABBOX
#include "tabbox.h"
#endif
#include "screenedge.h"
#include "screens.h"
#include "scripting/scriptedeffect.h"
#if KWIN_BUILD_SCREENLOCKER
#include "screenlockerwatcher.h"
#endif
#include "composite.h"
#include "decorations/decorationbridge.h"
#include "inputmethod.h"
#include "inputpanelv1window.h"
#include "kwinglutils.h"
#include "platform.h"
#include "utils/xcbutils.h"
#include "virtualdesktops.h"
#include "wayland_server.h"
#include "waylandwindow.h"
#include "window_property_notify_x11_filter.h"
#include "windowitem.h"
#include "workspace.h"

#include <KDecoration2/Decoration>
#include <KDecoration2/DecorationSettings>

#include <QDebug>
#include <QMouseEvent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QWheelEvent>

namespace KWin
{
//---------------------
// Static

static QByteArray readWindowProperty(xcb_window_t win, xcb_atom_t atom, xcb_atom_t type, int format)
{
    if (win == XCB_WINDOW_NONE) {
        return QByteArray();
    }
    uint32_t len = 32768;
    for (;;) {
        Xcb::Property prop(false, win, atom, XCB_ATOM_ANY, 0, len);
        if (prop.isNull()) {
            // get property failed
            return QByteArray();
        }
        if (prop->bytes_after > 0) {
            len *= 2;
            continue;
        }
        return prop.toByteArray(format, type);
    }
}

static void deleteWindowProperty(xcb_window_t win, long int atom)
{
    if (win == XCB_WINDOW_NONE) {
        return;
    }
    xcb_delete_property(kwinApp()->x11Connection(), win, atom);
}

static xcb_atom_t registerSupportProperty(const QByteArray &propertyName)
{
    auto c = kwinApp()->x11Connection();
    if (!c) {
        return XCB_ATOM_NONE;
    }
    // get the atom for the propertyName
    ScopedCPointer<xcb_intern_atom_reply_t> atomReply(xcb_intern_atom_reply(c,
                                                                            xcb_intern_atom_unchecked(c, false, propertyName.size(), propertyName.constData()),
                                                                            nullptr));
    if (atomReply.isNull()) {
        return XCB_ATOM_NONE;
    }
    // announce property on root window
    unsigned char dummy = 0;
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, kwinApp()->x11RootWindow(), atomReply->atom, atomReply->atom, 8, 1, &dummy);
    // TODO: add to _NET_SUPPORTED
    return atomReply->atom;
}

//---------------------

EffectsHandlerImpl::EffectsHandlerImpl(Compositor *compositor, Scene *scene)
    : EffectsHandler(Compositor::self()->backend()->compositingType())
    , keyboard_grab_effect(nullptr)
    , fullscreen_effect(nullptr)
    , m_compositor(compositor)
    , m_scene(scene)
    , m_effectLoader(new EffectLoader(this))
    , m_trackingCursorChanges(0)
{
    qRegisterMetaType<QVector<KWin::EffectWindow *>>();
    connect(m_effectLoader, &AbstractEffectLoader::effectLoaded, this, [this](Effect *effect, const QString &name) {
        effect_order.insert(effect->requestedEffectChainPosition(), EffectPair(name, effect));
        loaded_effects << EffectPair(name, effect);
        effectsChanged();
    });
    m_effectLoader->setConfig(kwinApp()->config());
    new EffectsAdaptor(this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject(QStringLiteral("/Effects"), this);

    Workspace *ws = Workspace::self();
    VirtualDesktopManager *vds = VirtualDesktopManager::self();
    connect(ws, &Workspace::showingDesktopChanged, this, [this](bool showing, bool animated) {
        if (animated) {
            Q_EMIT showingDesktopChanged(showing);
        }
    });
    connect(ws, &Workspace::currentDesktopChanged, this, [this](int old, Window *window) {
        const int newDesktop = VirtualDesktopManager::self()->current();
        if (old != 0 && newDesktop != old) {
            Q_EMIT desktopChanged(old, newDesktop, window ? window->effectWindow() : nullptr);
            // TODO: remove in 4.10
            Q_EMIT desktopChanged(old, newDesktop);
        }
    });
    connect(ws, &Workspace::currentDesktopChanging, this, [this](uint currentDesktop, QPointF offset, KWin::Window *window) {
        Q_EMIT desktopChanging(currentDesktop, offset, window ? window->effectWindow() : nullptr);
    });
    connect(ws, &Workspace::currentDesktopChangingCancelled, this, [this]() {
        Q_EMIT desktopChangingCancelled();
    });
    connect(ws, &Workspace::desktopPresenceChanged, this, [this](Window *window, int old) {
        if (!window->effectWindow()) {
            return;
        }
        Q_EMIT desktopPresenceChanged(window->effectWindow(), old, window->desktop());
    });
    connect(ws, &Workspace::windowAdded, this, [this](Window *window) {
        if (window->readyForPainting()) {
            slotWindowShown(window);
        } else {
            connect(window, &Window::windowShown, this, &EffectsHandlerImpl::slotWindowShown);
        }
    });
    connect(ws, &Workspace::unmanagedAdded, this, [this](Unmanaged *u) {
        // it's never initially ready but has synthetic 50ms delay
        connect(u, &Window::windowShown, this, &EffectsHandlerImpl::slotUnmanagedShown);
    });
    connect(ws, &Workspace::internalWindowAdded, this, [this](InternalWindow *window) {
        setupWindowConnections(window);
        Q_EMIT windowAdded(window->effectWindow());
    });
    connect(ws, &Workspace::windowActivated, this, [this](Window *window) {
        Q_EMIT windowActivated(window ? window->effectWindow() : nullptr);
    });
    connect(ws, &Workspace::deletedRemoved, this, [this](KWin::Window *d) {
        Q_EMIT windowDeleted(d->effectWindow());
        elevated_windows.removeAll(d->effectWindow());
    });
    connect(ws->sessionManager(), &SessionManager::stateChanged, this, &KWin::EffectsHandler::sessionStateChanged);
    connect(vds, &VirtualDesktopManager::countChanged, this, &EffectsHandler::numberDesktopsChanged);
    connect(vds, &VirtualDesktopManager::layoutChanged, this, [this](int width, int height) {
        Q_EMIT desktopGridSizeChanged(QSize(width, height));
        Q_EMIT desktopGridWidthChanged(width);
        Q_EMIT desktopGridHeightChanged(height);
    });
    connect(Cursors::self()->mouse(), &Cursor::mouseChanged, this, &EffectsHandler::mouseChanged);
    connect(Screens::self(), &Screens::sizeChanged, this, &EffectsHandler::virtualScreenSizeChanged);
    connect(Screens::self(), &Screens::geometryChanged, this, &EffectsHandler::virtualScreenGeometryChanged);
#if KWIN_BUILD_ACTIVITIES
    if (Activities *activities = Activities::self()) {
        connect(activities, &Activities::added, this, &EffectsHandler::activityAdded);
        connect(activities, &Activities::removed, this, &EffectsHandler::activityRemoved);
        connect(activities, &Activities::currentChanged, this, &EffectsHandler::currentActivityChanged);
    }
#endif
    connect(ws, &Workspace::stackingOrderChanged, this, &EffectsHandler::stackingOrderChanged);
#if KWIN_BUILD_TABBOX
    TabBox::TabBox *tabBox = TabBox::TabBox::self();
    connect(tabBox, &TabBox::TabBox::tabBoxAdded, this, &EffectsHandler::tabBoxAdded);
    connect(tabBox, &TabBox::TabBox::tabBoxUpdated, this, &EffectsHandler::tabBoxUpdated);
    connect(tabBox, &TabBox::TabBox::tabBoxClosed, this, &EffectsHandler::tabBoxClosed);
    connect(tabBox, &TabBox::TabBox::tabBoxKeyEvent, this, &EffectsHandler::tabBoxKeyEvent);
#endif
    connect(ScreenEdges::self(), &ScreenEdges::approaching, this, &EffectsHandler::screenEdgeApproaching);
#if KWIN_BUILD_SCREENLOCKER
    connect(ScreenLockerWatcher::self(), &ScreenLockerWatcher::locked, this, &EffectsHandler::screenLockingChanged);
    connect(ScreenLockerWatcher::self(), &ScreenLockerWatcher::aboutToLock, this, &EffectsHandler::screenAboutToLock);
#endif

    connect(kwinApp(), &Application::x11ConnectionChanged, this, [this]() {
        registered_atoms.clear();
        for (auto it = m_propertiesForEffects.keyBegin(); it != m_propertiesForEffects.keyEnd(); it++) {
            const auto atom = registerSupportProperty(*it);
            if (atom == XCB_ATOM_NONE) {
                continue;
            }
            m_compositor->keepSupportProperty(atom);
            m_managedProperties.insert(*it, atom);
            registerPropertyType(atom, true);
        }
        if (kwinApp()->x11Connection()) {
            m_x11WindowPropertyNotify = std::make_unique<WindowPropertyNotifyX11Filter>(this);
        } else {
            m_x11WindowPropertyNotify.reset();
        }
        Q_EMIT xcbConnectionChanged();
    });

    if (kwinApp()->x11Connection()) {
        m_x11WindowPropertyNotify = std::make_unique<WindowPropertyNotifyX11Filter>(this);
    }

    // connect all clients
    for (Window *window : ws->allClientList()) {
        if (window->readyForPainting()) {
            setupWindowConnections(window);
        } else {
            connect(window, &Window::windowShown, this, &EffectsHandlerImpl::slotWindowShown);
        }
    }
    for (Unmanaged *u : ws->unmanagedList()) {
        setupUnmanagedConnections(u);
    }
    for (InternalWindow *window : ws->internalWindows()) {
        setupWindowConnections(window);
    }

    connect(kwinApp()->platform(), &Platform::outputEnabled, this, &EffectsHandlerImpl::slotOutputEnabled);
    connect(kwinApp()->platform(), &Platform::outputDisabled, this, &EffectsHandlerImpl::slotOutputDisabled);

    const QVector<Output *> outputs = kwinApp()->platform()->enabledOutputs();
    for (Output *output : outputs) {
        slotOutputEnabled(output);
    }

    connect(InputMethod::self(), &InputMethod::panelChanged, this, &EffectsHandlerImpl::inputPanelChanged);

    reconfigure();
}

EffectsHandlerImpl::~EffectsHandlerImpl()
{
    unloadAllEffects();
}

void EffectsHandlerImpl::unloadAllEffects()
{
    for (const EffectPair &pair : qAsConst(loaded_effects)) {
        destroyEffect(pair.second);
    }

    effect_order.clear();
    m_effectLoader->clear();

    effectsChanged();
}

void EffectsHandlerImpl::setupWindowConnections(Window *window)
{
    connect(window, &Window::windowClosed, this, &EffectsHandlerImpl::slotWindowClosed);
    connect(window, static_cast<void (Window::*)(KWin::Window *, MaximizeMode)>(&Window::clientMaximizedStateChanged),
            this, &EffectsHandlerImpl::slotClientMaximized);
    connect(window, &Window::clientStartUserMovedResized, this, [this](Window *window) {
        Q_EMIT windowStartUserMovedResized(window->effectWindow());
    });
    connect(window, &Window::clientStepUserMovedResized, this, [this](Window *window, const QRect &geometry) {
        Q_EMIT windowStepUserMovedResized(window->effectWindow(), geometry);
    });
    connect(window, &Window::clientFinishUserMovedResized, this, [this](Window *window) {
        Q_EMIT windowFinishUserMovedResized(window->effectWindow());
    });
    connect(window, &Window::opacityChanged, this, &EffectsHandlerImpl::slotOpacityChanged);
    connect(window, &Window::clientMinimized, this, [this](Window *window, bool animate) {
        // TODO: notify effects even if it should not animate?
        if (animate) {
            Q_EMIT windowMinimized(window->effectWindow());
        }
    });
    connect(window, &Window::clientUnminimized, this, [this](Window *window, bool animate) {
        // TODO: notify effects even if it should not animate?
        if (animate) {
            Q_EMIT windowUnminimized(window->effectWindow());
        }
    });
    connect(window, &Window::modalChanged, this, &EffectsHandlerImpl::slotClientModalityChanged);
    connect(window, &Window::geometryShapeChanged, this, &EffectsHandlerImpl::slotGeometryShapeChanged);
    connect(window, &Window::frameGeometryChanged, this, &EffectsHandlerImpl::slotFrameGeometryChanged);
    connect(window, &Window::damaged, this, &EffectsHandlerImpl::slotWindowDamaged);
    connect(window, &Window::unresponsiveChanged, this, [this, window](bool unresponsive) {
        Q_EMIT windowUnresponsiveChanged(window->effectWindow(), unresponsive);
    });
    connect(window, &Window::windowShown, this, [this](Window *window) {
        Q_EMIT windowShown(window->effectWindow());
    });
    connect(window, &Window::windowHidden, this, [this](Window *window) {
        Q_EMIT windowHidden(window->effectWindow());
    });
    connect(window, &Window::keepAboveChanged, this, [this, window](bool above) {
        Q_UNUSED(above)
        Q_EMIT windowKeepAboveChanged(window->effectWindow());
    });
    connect(window, &Window::keepBelowChanged, this, [this, window](bool below) {
        Q_UNUSED(below)
        Q_EMIT windowKeepBelowChanged(window->effectWindow());
    });
    connect(window, &Window::fullScreenChanged, this, [this, window]() {
        Q_EMIT windowFullScreenChanged(window->effectWindow());
    });
    connect(window, &Window::visibleGeometryChanged, this, [this, window]() {
        Q_EMIT windowExpandedGeometryChanged(window->effectWindow());
    });
    connect(window, &Window::decorationChanged, this, [this, window]() {
        Q_EMIT windowDecorationChanged(window->effectWindow());
    });
}

void EffectsHandlerImpl::setupUnmanagedConnections(Unmanaged *u)
{
    connect(u, &Unmanaged::windowClosed, this, &EffectsHandlerImpl::slotWindowClosed);
    connect(u, &Unmanaged::opacityChanged, this, &EffectsHandlerImpl::slotOpacityChanged);
    connect(u, &Unmanaged::geometryShapeChanged, this, &EffectsHandlerImpl::slotGeometryShapeChanged);
    connect(u, &Unmanaged::frameGeometryChanged, this, &EffectsHandlerImpl::slotFrameGeometryChanged);
    connect(u, &Unmanaged::damaged, this, &EffectsHandlerImpl::slotWindowDamaged);
    connect(u, &Unmanaged::visibleGeometryChanged, this, [this, u]() {
        Q_EMIT windowExpandedGeometryChanged(u->effectWindow());
    });
}

void EffectsHandlerImpl::reconfigure()
{
    m_effectLoader->queryAndLoadAll();
}

// the idea is that effects call this function again which calls the next one
void EffectsHandlerImpl::prePaintScreen(ScreenPrePaintData &data, std::chrono::milliseconds presentTime)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->prePaintScreen(data, presentTime);
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void EffectsHandlerImpl::paintScreen(int mask, const QRegion &region, ScreenPaintData &data)
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->paintScreen(mask, region, data);
        --m_currentPaintScreenIterator;
    } else {
        m_scene->finalPaintScreen(mask, region, data);
    }
}

void EffectsHandlerImpl::postPaintScreen()
{
    if (m_currentPaintScreenIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintScreenIterator++)->postPaintScreen();
        --m_currentPaintScreenIterator;
    }
    // no special final code
}

void EffectsHandlerImpl::prePaintWindow(EffectWindow *w, WindowPrePaintData &data, std::chrono::milliseconds presentTime)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->prePaintWindow(w, data, presentTime);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

void EffectsHandlerImpl::paintWindow(EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->paintWindow(w, mask, region, data);
        --m_currentPaintWindowIterator;
    } else {
        m_scene->finalPaintWindow(static_cast<EffectWindowImpl *>(w), mask, region, data);
    }
}

void EffectsHandlerImpl::postPaintWindow(EffectWindow *w)
{
    if (m_currentPaintWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentPaintWindowIterator++)->postPaintWindow(w);
        --m_currentPaintWindowIterator;
    }
    // no special final code
}

Effect *EffectsHandlerImpl::provides(Effect::Feature ef)
{
    for (int i = 0; i < loaded_effects.size(); ++i) {
        if (loaded_effects.at(i).second->provides(ef)) {
            return loaded_effects.at(i).second;
        }
    }
    return nullptr;
}

void EffectsHandlerImpl::drawWindow(EffectWindow *w, int mask, const QRegion &region, WindowPaintData &data)
{
    if (m_currentDrawWindowIterator != m_activeEffects.constEnd()) {
        (*m_currentDrawWindowIterator++)->drawWindow(w, mask, region, data);
        --m_currentDrawWindowIterator;
    } else {
        m_scene->finalDrawWindow(static_cast<EffectWindowImpl *>(w), mask, region, data);
    }
}

bool EffectsHandlerImpl::hasDecorationShadows() const
{
    return false;
}

bool EffectsHandlerImpl::decorationsHaveAlpha() const
{
    return true;
}

// start another painting pass
void EffectsHandlerImpl::startPaint()
{
    m_activeEffects.clear();
    m_activeEffects.reserve(loaded_effects.count());
    for (QVector<KWin::EffectPair>::const_iterator it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->isActive()) {
            m_activeEffects << it->second;
        }
    }
    m_currentDrawWindowIterator = m_activeEffects.constBegin();
    m_currentPaintWindowIterator = m_activeEffects.constBegin();
    m_currentPaintScreenIterator = m_activeEffects.constBegin();
}

void EffectsHandlerImpl::slotClientMaximized(Window *window, MaximizeMode maxMode)
{
    bool horizontal = false;
    bool vertical = false;
    switch (maxMode) {
    case MaximizeHorizontal:
        horizontal = true;
        break;
    case MaximizeVertical:
        vertical = true;
        break;
    case MaximizeFull:
        horizontal = true;
        vertical = true;
        break;
    case MaximizeRestore: // fall through
    default:
        // default - nothing to do
        break;
    }
    if (EffectWindowImpl *w = window->effectWindow()) {
        Q_EMIT windowMaximizedStateChanged(w, horizontal, vertical);
    }
}

void EffectsHandlerImpl::slotOpacityChanged(Window *window, qreal oldOpacity)
{
    if (window->opacity() == oldOpacity || !window->effectWindow()) {
        return;
    }
    Q_EMIT windowOpacityChanged(window->effectWindow(), oldOpacity, (qreal)window->opacity());
}

void EffectsHandlerImpl::slotWindowShown(Window *window)
{
    Q_ASSERT(window->isClient());
    disconnect(window, &Window::windowShown, this, &EffectsHandlerImpl::slotWindowShown);
    setupWindowConnections(window);
    Q_EMIT windowAdded(window->effectWindow());
}

void EffectsHandlerImpl::slotUnmanagedShown(Window *window)
{ // regardless, unmanaged windows are -yet?- not synced anyway
    Q_ASSERT(qobject_cast<Unmanaged *>(window));
    Unmanaged *u = static_cast<Unmanaged *>(window);
    setupUnmanagedConnections(u);
    Q_EMIT windowAdded(u->effectWindow());
}

void EffectsHandlerImpl::slotWindowClosed(Window *window)
{
    window->disconnect(this);
    Q_EMIT windowClosed(window->effectWindow());
}

void EffectsHandlerImpl::slotClientModalityChanged()
{
    Q_EMIT windowModalityChanged(static_cast<X11Window *>(sender())->effectWindow());
}

void EffectsHandlerImpl::slotCurrentTabAboutToChange(EffectWindow *from, EffectWindow *to)
{
    Q_EMIT currentTabAboutToChange(from, to);
}

void EffectsHandlerImpl::slotTabAdded(EffectWindow *w, EffectWindow *to)
{
    Q_EMIT tabAdded(w, to);
}

void EffectsHandlerImpl::slotTabRemoved(EffectWindow *w, EffectWindow *leaderOfFormerGroup)
{
    Q_EMIT tabRemoved(w, leaderOfFormerGroup);
}

void EffectsHandlerImpl::slotWindowDamaged(Window *window, const QRegion &r)
{
    if (!window->effectWindow()) {
        // can happen during tear down of window
        return;
    }
    Q_EMIT windowDamaged(window->effectWindow(), r);
}

void EffectsHandlerImpl::slotGeometryShapeChanged(Window *window, const QRect &old)
{
    // during late cleanup effectWindow() may be already NULL
    // in some functions that may still call this
    if (window == nullptr || window->effectWindow() == nullptr) {
        return;
    }
    Q_EMIT windowGeometryShapeChanged(window->effectWindow(), old);
}

void EffectsHandlerImpl::slotFrameGeometryChanged(Window *window, const QRect &oldGeometry)
{
    // effectWindow() might be nullptr during tear down of the client.
    if (window->effectWindow()) {
        Q_EMIT windowFrameGeometryChanged(window->effectWindow(), oldGeometry);
    }
}

void EffectsHandlerImpl::setActiveFullScreenEffect(Effect *e)
{
    if (fullscreen_effect == e) {
        return;
    }
    const bool activeChanged = (e == nullptr || fullscreen_effect == nullptr);
    fullscreen_effect = e;
    Q_EMIT activeFullScreenEffectChanged();
    if (activeChanged) {
        const auto delegates = m_scene->delegates();
        for (SceneDelegate *delegate : delegates) {
            RenderLoop *loop = delegate->layer()->loop();
            if (fullscreen_effect) {
                loop->setLatencyPolicy(LatencyPolicy::LatencyExtremelyHigh);
            } else {
                loop->resetLatencyPolicy();
            }
        }
        Q_EMIT hasActiveFullScreenEffectChanged();
        ScreenEdges::self()->checkBlocking();
    }
}

Effect *EffectsHandlerImpl::activeFullScreenEffect() const
{
    return fullscreen_effect;
}

bool EffectsHandlerImpl::hasActiveFullScreenEffect() const
{
    return fullscreen_effect;
}

bool EffectsHandlerImpl::grabKeyboard(Effect *effect)
{
    if (keyboard_grab_effect != nullptr) {
        return false;
    }
    if (!doGrabKeyboard()) {
        return false;
    }
    keyboard_grab_effect = effect;
    return true;
}

bool EffectsHandlerImpl::doGrabKeyboard()
{
    return true;
}

void EffectsHandlerImpl::ungrabKeyboard()
{
    Q_ASSERT(keyboard_grab_effect != nullptr);
    doUngrabKeyboard();
    keyboard_grab_effect = nullptr;
}

void EffectsHandlerImpl::doUngrabKeyboard()
{
}

void EffectsHandlerImpl::grabbedKeyboardEvent(QKeyEvent *e)
{
    if (keyboard_grab_effect != nullptr) {
        keyboard_grab_effect->grabbedKeyboardEvent(e);
    }
}

void EffectsHandlerImpl::startMouseInterception(Effect *effect, Qt::CursorShape shape)
{
    if (m_grabbedMouseEffects.contains(effect)) {
        return;
    }
    m_grabbedMouseEffects.append(effect);
    if (m_grabbedMouseEffects.size() != 1) {
        return;
    }
    doStartMouseInterception(shape);
}

void EffectsHandlerImpl::doStartMouseInterception(Qt::CursorShape shape)
{
    input()->pointer()->setEffectsOverrideCursor(shape);

    // We want to allow global shortcuts to be triggered when moving a
    // window so it is possible to pick up a window and then move it to a
    // different desktop by using the global shortcut to switch desktop.
    // However, that means that some other things can also be triggered. If
    // an effect that fill the screen gets triggered that way, we end up in a
    // weird state where the move will restart after the effect closes. So to
    // avoid that, abort move/resize if a full screen effect starts.
    if (workspace()->moveResizeWindow()) {
        workspace()->moveResizeWindow()->endInteractiveMoveResize();
    }
}

void EffectsHandlerImpl::stopMouseInterception(Effect *effect)
{
    if (!m_grabbedMouseEffects.contains(effect)) {
        return;
    }
    m_grabbedMouseEffects.removeAll(effect);
    if (m_grabbedMouseEffects.isEmpty()) {
        doStopMouseInterception();
    }
}

void EffectsHandlerImpl::doStopMouseInterception()
{
    input()->pointer()->removeEffectsOverrideCursor();
}

bool EffectsHandlerImpl::isMouseInterception() const
{
    return m_grabbedMouseEffects.count() > 0;
}

bool EffectsHandlerImpl::touchDown(qint32 id, const QPointF &pos, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchDown(id, pos, time)) {
            return true;
        }
    }
    return false;
}

bool EffectsHandlerImpl::touchMotion(qint32 id, const QPointF &pos, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchMotion(id, pos, time)) {
            return true;
        }
    }
    return false;
}

bool EffectsHandlerImpl::touchUp(qint32 id, quint32 time)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->touchUp(id, time)) {
            return true;
        }
    }
    return false;
}

bool EffectsHandlerImpl::tabletToolEvent(TabletEvent *event)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->tabletToolEvent(event)) {
            return true;
        }
    }
    return false;
}

bool EffectsHandlerImpl::tabletToolButtonEvent(uint button, bool pressed, const TabletToolId &tabletToolId)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->tabletToolButtonEvent(button, pressed, tabletToolId.m_uniqueId)) {
            return true;
        }
    }
    return false;
}

bool EffectsHandlerImpl::tabletPadButtonEvent(uint button, bool pressed, const TabletPadId &tabletPadId)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->tabletPadButtonEvent(button, pressed, tabletPadId.data)) {
            return true;
        }
    }
    return false;
}

bool EffectsHandlerImpl::tabletPadStripEvent(int number, int position, bool isFinger, const TabletPadId &tabletPadId)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->tabletPadStripEvent(number, position, isFinger, tabletPadId.data)) {
            return true;
        }
    }
    return false;
}

bool EffectsHandlerImpl::tabletPadRingEvent(int number, int position, bool isFinger, const TabletPadId &tabletPadId)
{
    // TODO: reverse call order?
    for (auto it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if (it->second->tabletPadRingEvent(number, position, isFinger, tabletPadId.data)) {
            return true;
        }
    }
    return false;
}

void EffectsHandlerImpl::registerGlobalShortcut(const QKeySequence &shortcut, QAction *action)
{
    input()->registerShortcut(shortcut, action);
}

void EffectsHandlerImpl::registerPointerShortcut(Qt::KeyboardModifiers modifiers, Qt::MouseButton pointerButtons, QAction *action)
{
    input()->registerPointerShortcut(modifiers, pointerButtons, action);
}

void EffectsHandlerImpl::registerAxisShortcut(Qt::KeyboardModifiers modifiers, PointerAxisDirection axis, QAction *action)
{
    input()->registerAxisShortcut(modifiers, axis, action);
}

void EffectsHandlerImpl::registerRealtimeTouchpadSwipeShortcut(SwipeDirection dir, uint fingerCount, QAction *onUp, std::function<void(qreal)> progressCallback)
{
    input()->registerRealtimeTouchpadSwipeShortcut(dir, fingerCount, onUp, progressCallback);
}

void EffectsHandlerImpl::registerTouchpadSwipeShortcut(SwipeDirection direction, uint fingerCount, QAction *action)
{
    input()->registerTouchpadSwipeShortcut(direction, fingerCount, action);
}

void EffectsHandlerImpl::registerRealtimeTouchpadPinchShortcut(PinchDirection dir, uint fingerCount, QAction *onUp, std::function<void(qreal)> progressCallback)
{
    input()->registerRealtimeTouchpadPinchShortcut(dir, fingerCount, onUp, progressCallback);
}

void EffectsHandlerImpl::registerTouchpadPinchShortcut(PinchDirection direction, uint fingerCount, QAction *action)
{
    input()->registerTouchpadPinchShortcut(direction, fingerCount, action);
}

void EffectsHandlerImpl::registerTouchscreenSwipeShortcut(SwipeDirection direction, uint fingerCount, QAction *action, std::function<void(qreal)> progressCallback)
{
    input()->registerTouchscreenSwipeShortcut(direction, fingerCount, action, progressCallback);
}

void *EffectsHandlerImpl::getProxy(QString name)
{
    for (QVector<EffectPair>::const_iterator it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if ((*it).first == name) {
            return (*it).second->proxy();
        }
    }

    return nullptr;
}

void EffectsHandlerImpl::startMousePolling()
{
    if (Cursors::self()->mouse()) {
        Cursors::self()->mouse()->startMousePolling();
    }
}

void EffectsHandlerImpl::stopMousePolling()
{
    if (Cursors::self()->mouse()) {
        Cursors::self()->mouse()->stopMousePolling();
    }
}

bool EffectsHandlerImpl::hasKeyboardGrab() const
{
    return keyboard_grab_effect != nullptr;
}

void EffectsHandlerImpl::registerPropertyType(long atom, bool reg)
{
    if (reg) {
        ++registered_atoms[atom]; // initialized to 0 if not present yet
    } else {
        if (--registered_atoms[atom] == 0) {
            registered_atoms.remove(atom);
        }
    }
}

xcb_atom_t EffectsHandlerImpl::announceSupportProperty(const QByteArray &propertyName, Effect *effect)
{
    PropertyEffectMap::iterator it = m_propertiesForEffects.find(propertyName);
    if (it != m_propertiesForEffects.end()) {
        // property has already been registered for an effect
        // just append Effect and return the atom stored in m_managedProperties
        if (!it.value().contains(effect)) {
            it.value().append(effect);
        }
        return m_managedProperties.value(propertyName, XCB_ATOM_NONE);
    }
    m_propertiesForEffects.insert(propertyName, QList<Effect *>() << effect);
    const auto atom = registerSupportProperty(propertyName);
    if (atom == XCB_ATOM_NONE) {
        return atom;
    }
    m_compositor->keepSupportProperty(atom);
    m_managedProperties.insert(propertyName, atom);
    registerPropertyType(atom, true);
    return atom;
}

void EffectsHandlerImpl::removeSupportProperty(const QByteArray &propertyName, Effect *effect)
{
    PropertyEffectMap::iterator it = m_propertiesForEffects.find(propertyName);
    if (it == m_propertiesForEffects.end()) {
        // property is not registered - nothing to do
        return;
    }
    if (!it.value().contains(effect)) {
        // property is not registered for given effect - nothing to do
        return;
    }
    it.value().removeAll(effect);
    if (!it.value().isEmpty()) {
        // property still registered for another effect - nothing further to do
        return;
    }
    const xcb_atom_t atom = m_managedProperties.take(propertyName);
    registerPropertyType(atom, false);
    m_propertiesForEffects.remove(propertyName);
    m_compositor->removeSupportProperty(atom); // delayed removal
}

QByteArray EffectsHandlerImpl::readRootProperty(long atom, long type, int format) const
{
    if (!kwinApp()->x11Connection()) {
        return QByteArray();
    }
    return readWindowProperty(kwinApp()->x11RootWindow(), atom, type, format);
}

void EffectsHandlerImpl::activateWindow(EffectWindow *effectWindow)
{
    auto window = static_cast<EffectWindowImpl *>(effectWindow)->window();
    if (window->isClient()) {
        Workspace::self()->activateWindow(window, true);
    }
}

EffectWindow *EffectsHandlerImpl::activeWindow() const
{
    return Workspace::self()->activeWindow() ? Workspace::self()->activeWindow()->effectWindow() : nullptr;
}

void EffectsHandlerImpl::moveWindow(EffectWindow *w, const QPoint &pos, bool snap, double snapAdjust)
{
    auto window = static_cast<EffectWindowImpl *>(w)->window();
    if (!window->isClient() || !window->isMovable()) {
        return;
    }

    if (snap) {
        window->move(Workspace::self()->adjustWindowPosition(window, pos, true, snapAdjust));
    } else {
        window->move(pos);
    }
}

void EffectsHandlerImpl::windowToDesktop(EffectWindow *w, int desktop)
{
    auto window = static_cast<EffectWindowImpl *>(w)->window();
    if (window->isClient() && !window->isDesktop() && !window->isDock()) {
        Workspace::self()->sendWindowToDesktop(window, desktop, true);
    }
}

void EffectsHandlerImpl::windowToDesktops(EffectWindow *w, const QVector<uint> &desktopIds)
{
    auto window = static_cast<EffectWindowImpl *>(w)->window();
    if (!window->isClient() || window->isDesktop() || window->isDock()) {
        return;
    }
    QVector<VirtualDesktop *> desktops;
    desktops.reserve(desktopIds.count());
    for (uint x11Id : desktopIds) {
        if (x11Id > VirtualDesktopManager::self()->count()) {
            continue;
        }
        VirtualDesktop *d = VirtualDesktopManager::self()->desktopForX11Id(x11Id);
        Q_ASSERT(d);
        if (desktops.contains(d)) {
            continue;
        }
        desktops << d;
    }
    window->setDesktops(desktops);
}

void EffectsHandlerImpl::windowToScreen(EffectWindow *w, EffectScreen *screen)
{
    auto window = static_cast<EffectWindowImpl *>(w)->window();
    if (window->isClient() && !window->isDesktop() && !window->isDock()) {
        EffectScreenImpl *screenImpl = static_cast<EffectScreenImpl *>(screen);
        Workspace::self()->sendWindowToOutput(window, screenImpl->platformOutput());
    }
}

void EffectsHandlerImpl::setShowingDesktop(bool showing)
{
    Workspace::self()->setShowingDesktop(showing);
}

QString EffectsHandlerImpl::currentActivity() const
{
#if KWIN_BUILD_ACTIVITIES
    if (!Activities::self()) {
        return QString();
    }
    return Activities::self()->current();
#else
    return QString();
#endif
}

int EffectsHandlerImpl::currentDesktop() const
{
    return VirtualDesktopManager::self()->current();
}

int EffectsHandlerImpl::numberOfDesktops() const
{
    return VirtualDesktopManager::self()->count();
}

void EffectsHandlerImpl::setCurrentDesktop(int desktop)
{
    VirtualDesktopManager::self()->setCurrent(desktop);
}

void EffectsHandlerImpl::setNumberOfDesktops(int desktops)
{
    VirtualDesktopManager::self()->setCount(desktops);
}

QSize EffectsHandlerImpl::desktopGridSize() const
{
    return VirtualDesktopManager::self()->grid().size();
}

int EffectsHandlerImpl::desktopGridWidth() const
{
    return desktopGridSize().width();
}

int EffectsHandlerImpl::desktopGridHeight() const
{
    return desktopGridSize().height();
}

int EffectsHandlerImpl::workspaceWidth() const
{
    return desktopGridWidth() * Screens::self()->size().width();
}

int EffectsHandlerImpl::workspaceHeight() const
{
    return desktopGridHeight() * Screens::self()->size().height();
}

int EffectsHandlerImpl::desktopAtCoords(QPoint coords) const
{
    if (auto vd = VirtualDesktopManager::self()->grid().at(coords)) {
        return vd->x11DesktopNumber();
    }
    return 0;
}

QPoint EffectsHandlerImpl::desktopGridCoords(int id) const
{
    return VirtualDesktopManager::self()->grid().gridCoords(id);
}

QPoint EffectsHandlerImpl::desktopCoords(int id) const
{
    QPoint coords = VirtualDesktopManager::self()->grid().gridCoords(id);
    if (coords.x() == -1) {
        return QPoint(-1, -1);
    }
    const QSize displaySize = Screens::self()->size();
    return QPoint(coords.x() * displaySize.width(), coords.y() * displaySize.height());
}

int EffectsHandlerImpl::desktopAbove(int desktop, bool wrap) const
{
    return getDesktop<DesktopAbove>(desktop, wrap);
}

int EffectsHandlerImpl::desktopToRight(int desktop, bool wrap) const
{
    return getDesktop<DesktopRight>(desktop, wrap);
}

int EffectsHandlerImpl::desktopBelow(int desktop, bool wrap) const
{
    return getDesktop<DesktopBelow>(desktop, wrap);
}

int EffectsHandlerImpl::desktopToLeft(int desktop, bool wrap) const
{
    return getDesktop<DesktopLeft>(desktop, wrap);
}

QString EffectsHandlerImpl::desktopName(int desktop) const
{
    const VirtualDesktop *vd = VirtualDesktopManager::self()->desktopForX11Id(desktop);
    return vd ? vd->name() : QString();
}

bool EffectsHandlerImpl::optionRollOverDesktops() const
{
    return options->isRollOverDesktops();
}

double EffectsHandlerImpl::animationTimeFactor() const
{
    return options->animationTimeFactor();
}

EffectWindow *EffectsHandlerImpl::findWindow(WId id) const
{
    if (X11Window *w = Workspace::self()->findClient(Predicate::WindowMatch, id)) {
        return w->effectWindow();
    }
    if (Unmanaged *w = Workspace::self()->findUnmanaged(id)) {
        return w->effectWindow();
    }
    return nullptr;
}

EffectWindow *EffectsHandlerImpl::findWindow(KWaylandServer::SurfaceInterface *surf) const
{
    if (waylandServer()) {
        if (Window *w = waylandServer()->findWindow(surf)) {
            return w->effectWindow();
        }
    }
    return nullptr;
}

EffectWindow *EffectsHandlerImpl::findWindow(QWindow *w) const
{
    if (Window *window = workspace()->findInternal(w)) {
        return window->effectWindow();
    }
    return nullptr;
}

EffectWindow *EffectsHandlerImpl::findWindow(const QUuid &id) const
{
    if (Window *window = workspace()->findToplevel(id)) {
        return window->effectWindow();
    }
    return nullptr;
}

EffectWindowList EffectsHandlerImpl::stackingOrder() const
{
    QList<Window *> list = workspace()->stackingOrder();
    EffectWindowList ret;
    for (Window *t : list) {
        if (EffectWindow *w = t->effectWindow()) {
            ret.append(w);
        }
    }
    return ret;
}

void EffectsHandlerImpl::setElevatedWindow(KWin::EffectWindow *w, bool set)
{
    elevated_windows.removeAll(w);
    if (set) {
        elevated_windows.append(w);
    }
}

void EffectsHandlerImpl::setTabBoxWindow(EffectWindow *w)
{
#if KWIN_BUILD_TABBOX
    auto window = static_cast<EffectWindowImpl *>(w)->window();
    if (window->isClient()) {
        TabBox::TabBox::self()->setCurrentClient(window);
    }
#else
    Q_UNUSED(w)
#endif
}

void EffectsHandlerImpl::setTabBoxDesktop(int desktop)
{
#if KWIN_BUILD_TABBOX
    TabBox::TabBox::self()->setCurrentDesktop(desktop);
#else
    Q_UNUSED(desktop)
#endif
}

EffectWindowList EffectsHandlerImpl::currentTabBoxWindowList() const
{
#if KWIN_BUILD_TABBOX
    const auto clients = TabBox::TabBox::self()->currentClientList();
    EffectWindowList ret;
    ret.reserve(clients.size());
    std::transform(std::cbegin(clients), std::cend(clients),
                   std::back_inserter(ret),
                   [](auto client) {
                       return client->effectWindow();
                   });
    return ret;
#else
    return EffectWindowList();
#endif
}

void EffectsHandlerImpl::refTabBox()
{
#if KWIN_BUILD_TABBOX
    TabBox::TabBox::self()->reference();
#endif
}

void EffectsHandlerImpl::unrefTabBox()
{
#if KWIN_BUILD_TABBOX
    TabBox::TabBox::self()->unreference();
#endif
}

void EffectsHandlerImpl::closeTabBox()
{
#if KWIN_BUILD_TABBOX
    TabBox::TabBox::self()->close();
#endif
}

QList<int> EffectsHandlerImpl::currentTabBoxDesktopList() const
{
#if KWIN_BUILD_TABBOX
    return TabBox::TabBox::self()->currentDesktopList();
#else
    return QList<int>();
#endif
}

int EffectsHandlerImpl::currentTabBoxDesktop() const
{
#if KWIN_BUILD_TABBOX
    return TabBox::TabBox::self()->currentDesktop();
#else
    return -1;
#endif
}

EffectWindow *EffectsHandlerImpl::currentTabBoxWindow() const
{
#if KWIN_BUILD_TABBOX
    if (auto c = TabBox::TabBox::self()->currentClient()) {
        return c->effectWindow();
    }
#endif
    return nullptr;
}

void EffectsHandlerImpl::addRepaintFull()
{
    m_compositor->scene()->addRepaintFull();
}

void EffectsHandlerImpl::addRepaint(const QRect &r)
{
    m_compositor->scene()->addRepaint(r);
}

void EffectsHandlerImpl::addRepaint(const QRegion &r)
{
    m_compositor->scene()->addRepaint(r);
}

void EffectsHandlerImpl::addRepaint(int x, int y, int w, int h)
{
    m_compositor->scene()->addRepaint(x, y, w, h);
}

EffectScreen *EffectsHandlerImpl::activeScreen() const
{
    return EffectScreenImpl::get(workspace()->activeOutput());
}

static VirtualDesktop *resolveVirtualDesktop(int desktopId)
{
    if (desktopId == 0 || desktopId == -1) {
        return VirtualDesktopManager::self()->currentDesktop();
    } else {
        return VirtualDesktopManager::self()->desktopForX11Id(desktopId);
    }
}

QRect EffectsHandlerImpl::clientArea(clientAreaOption opt, const EffectScreen *screen, int desktop) const
{
    const EffectScreenImpl *screenImpl = static_cast<const EffectScreenImpl *>(screen);
    return Workspace::self()->clientArea(opt, screenImpl->platformOutput(), resolveVirtualDesktop(desktop));
}

QRect EffectsHandlerImpl::clientArea(clientAreaOption opt, const EffectWindow *effectWindow) const
{
    const Window *window = static_cast<const EffectWindowImpl *>(effectWindow)->window();
    return Workspace::self()->clientArea(opt, window);
}

QRect EffectsHandlerImpl::clientArea(clientAreaOption opt, const QPoint &p, int desktop) const
{
    const Output *output = kwinApp()->platform()->outputAt(p);
    const VirtualDesktop *virtualDesktop = resolveVirtualDesktop(desktop);
    return Workspace::self()->clientArea(opt, output, virtualDesktop);
}

QRect EffectsHandlerImpl::virtualScreenGeometry() const
{
    return Screens::self()->geometry();
}

QSize EffectsHandlerImpl::virtualScreenSize() const
{
    return Screens::self()->size();
}

void EffectsHandlerImpl::defineCursor(Qt::CursorShape shape)
{
    input()->pointer()->setEffectsOverrideCursor(shape);
}

bool EffectsHandlerImpl::checkInputWindowEvent(QMouseEvent *e)
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return false;
    }
    for (Effect *effect : qAsConst(m_grabbedMouseEffects)) {
        effect->windowInputMouseEvent(e);
    }
    return true;
}

bool EffectsHandlerImpl::checkInputWindowEvent(QWheelEvent *e)
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return false;
    }
    for (Effect *effect : qAsConst(m_grabbedMouseEffects)) {
        effect->windowInputMouseEvent(e);
    }
    return true;
}

void EffectsHandlerImpl::connectNotify(const QMetaMethod &signal)
{
    if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
        if (!m_trackingCursorChanges) {
            connect(Cursors::self()->mouse(), &Cursor::cursorChanged, this, &EffectsHandler::cursorShapeChanged);
            Cursors::self()->mouse()->startCursorTracking();
        }
        ++m_trackingCursorChanges;
    }
    EffectsHandler::connectNotify(signal);
}

void EffectsHandlerImpl::disconnectNotify(const QMetaMethod &signal)
{
    if (signal == QMetaMethod::fromSignal(&EffectsHandler::cursorShapeChanged)) {
        Q_ASSERT(m_trackingCursorChanges > 0);
        if (!--m_trackingCursorChanges) {
            Cursors::self()->mouse()->stopCursorTracking();
            disconnect(Cursors::self()->mouse(), &Cursor::cursorChanged, this, &EffectsHandler::cursorShapeChanged);
        }
    }
    EffectsHandler::disconnectNotify(signal);
}

void EffectsHandlerImpl::checkInputWindowStacking()
{
    if (m_grabbedMouseEffects.isEmpty()) {
        return;
    }
    doCheckInputWindowStacking();
}

void EffectsHandlerImpl::doCheckInputWindowStacking()
{
}

QPoint EffectsHandlerImpl::cursorPos() const
{
    return Cursors::self()->mouse()->pos();
}

void EffectsHandlerImpl::reserveElectricBorder(ElectricBorder border, Effect *effect)
{
    ScreenEdges::self()->reserve(border, effect, "borderActivated");
}

void EffectsHandlerImpl::unreserveElectricBorder(ElectricBorder border, Effect *effect)
{
    ScreenEdges::self()->unreserve(border, effect);
}

void EffectsHandlerImpl::registerTouchBorder(ElectricBorder border, QAction *action)
{
    ScreenEdges::self()->reserveTouch(border, action);
}

void EffectsHandlerImpl::registerRealtimeTouchBorder(ElectricBorder border, QAction *action, EffectsHandler::TouchBorderCallback progressCallback)
{
    ScreenEdges::self()->reserveTouch(border, action, [progressCallback](ElectricBorder border, const QSizeF &deltaProgress, Output *output) {
        progressCallback(border, deltaProgress, EffectScreenImpl::get(output));
    });
}

void EffectsHandlerImpl::unregisterTouchBorder(ElectricBorder border, QAction *action)
{
    ScreenEdges::self()->unreserveTouch(border, action);
}

QPainter *EffectsHandlerImpl::scenePainter()
{
    return m_scene->scenePainter();
}

void EffectsHandlerImpl::toggleEffect(const QString &name)
{
    if (isEffectLoaded(name)) {
        unloadEffect(name);
    } else {
        loadEffect(name);
    }
}

QStringList EffectsHandlerImpl::loadedEffects() const
{
    QStringList listModules;
    listModules.reserve(loaded_effects.count());
    std::transform(loaded_effects.constBegin(), loaded_effects.constEnd(),
                   std::back_inserter(listModules),
                   [](const EffectPair &pair) {
                       return pair.first;
                   });
    return listModules;
}

QStringList EffectsHandlerImpl::listOfEffects() const
{
    return m_effectLoader->listOfKnownEffects();
}

bool EffectsHandlerImpl::loadEffect(const QString &name)
{
    makeOpenGLContextCurrent();
    m_compositor->scene()->addRepaintFull();

    return m_effectLoader->loadEffect(name);
}

void EffectsHandlerImpl::unloadEffect(const QString &name)
{
    auto it = std::find_if(effect_order.begin(), effect_order.end(),
                           [name](EffectPair &pair) {
                               return pair.first == name;
                           });
    if (it == effect_order.end()) {
        qCDebug(KWIN_CORE) << "EffectsHandler::unloadEffect : Effect not loaded :" << name;
        return;
    }

    qCDebug(KWIN_CORE) << "EffectsHandler::unloadEffect : Unloading Effect :" << name;
    destroyEffect((*it).second);
    effect_order.erase(it);
    effectsChanged();

    m_compositor->scene()->addRepaintFull();
}

void EffectsHandlerImpl::destroyEffect(Effect *effect)
{
    makeOpenGLContextCurrent();

    if (fullscreen_effect == effect) {
        setActiveFullScreenEffect(nullptr);
    }

    if (keyboard_grab_effect == effect) {
        ungrabKeyboard();
    }

    stopMouseInterception(effect);

    const QList<QByteArray> properties = m_propertiesForEffects.keys();
    for (const QByteArray &property : properties) {
        removeSupportProperty(property, effect);
    }

    delete effect;
}

void EffectsHandlerImpl::reconfigureEffect(const QString &name)
{
    for (QVector<EffectPair>::const_iterator it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if ((*it).first == name) {
            kwinApp()->config()->reparseConfiguration();
            makeOpenGLContextCurrent();
            (*it).second->reconfigure(Effect::ReconfigureAll);
            return;
        }
    }
}

bool EffectsHandlerImpl::isEffectLoaded(const QString &name) const
{
    auto it = std::find_if(loaded_effects.constBegin(), loaded_effects.constEnd(),
                           [&name](const EffectPair &pair) {
                               return pair.first == name;
                           });
    return it != loaded_effects.constEnd();
}

bool EffectsHandlerImpl::isEffectSupported(const QString &name)
{
    // If the effect is loaded, it is obviously supported.
    if (isEffectLoaded(name)) {
        return true;
    }

    // next checks might require a context
    makeOpenGLContextCurrent();

    return m_effectLoader->isEffectSupported(name);
}

QList<bool> EffectsHandlerImpl::areEffectsSupported(const QStringList &names)
{
    QList<bool> retList;
    retList.reserve(names.count());
    std::transform(names.constBegin(), names.constEnd(),
                   std::back_inserter(retList),
                   [this](const QString &name) {
                       return isEffectSupported(name);
                   });
    return retList;
}

void EffectsHandlerImpl::reloadEffect(Effect *effect)
{
    QString effectName;
    for (QVector<EffectPair>::const_iterator it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if ((*it).second == effect) {
            effectName = (*it).first;
            break;
        }
    }
    if (!effectName.isNull()) {
        unloadEffect(effectName);
        m_effectLoader->loadEffect(effectName);
    }
}

void EffectsHandlerImpl::effectsChanged()
{
    loaded_effects.clear();
    m_activeEffects.clear(); // it's possible to have a reconfigure and a quad rebuild between two paint cycles - bug #308201

    loaded_effects.reserve(effect_order.count());
    std::copy(effect_order.constBegin(), effect_order.constEnd(),
              std::back_inserter(loaded_effects));

    m_activeEffects.reserve(loaded_effects.count());
}

QStringList EffectsHandlerImpl::activeEffects() const
{
    QStringList ret;
    for (QVector<KWin::EffectPair>::const_iterator it = loaded_effects.constBegin(),
                                                   end = loaded_effects.constEnd();
         it != end; ++it) {
        if (it->second->isActive()) {
            ret << it->first;
        }
    }
    return ret;
}

bool EffectsHandlerImpl::blocksDirectScanout() const
{
    return std::any_of(m_activeEffects.constBegin(), m_activeEffects.constEnd(), [](const Effect *effect) {
        return effect->blocksDirectScanout();
    });
}

KWaylandServer::Display *EffectsHandlerImpl::waylandDisplay() const
{
    if (waylandServer()) {
        return waylandServer()->display();
    }
    return nullptr;
}

std::unique_ptr<EffectFrame> EffectsHandlerImpl::effectFrame(EffectFrameStyle style, bool staticSize, const QPoint &position, Qt::Alignment alignment) const
{
    return std::make_unique<EffectFrameImpl>(style, staticSize, position, alignment);
}

QVariant EffectsHandlerImpl::kwinOption(KWinOption kwopt)
{
    switch (kwopt) {
    case CloseButtonCorner: {
        // TODO: this could become per window and be derived from the actual position in the deco
        const auto settings = Decoration::DecorationBridge::self()->settings();
        return settings && settings->decorationButtonsLeft().contains(KDecoration2::DecorationButtonType::Close) ? Qt::TopLeftCorner : Qt::TopRightCorner;
    }
    case SwitchDesktopOnScreenEdge:
        return ScreenEdges::self()->isDesktopSwitching();
    case SwitchDesktopOnScreenEdgeMovingWindows:
        return ScreenEdges::self()->isDesktopSwitchingMovingClients();
    default:
        return QVariant(); // an invalid one
    }
}

QString EffectsHandlerImpl::supportInformation(const QString &name) const
{
    auto it = std::find_if(loaded_effects.constBegin(), loaded_effects.constEnd(),
                           [name](const EffectPair &pair) {
                               return pair.first == name;
                           });
    if (it == loaded_effects.constEnd()) {
        return QString();
    }

    QString support((*it).first + QLatin1String(":\n"));
    const QMetaObject *metaOptions = (*it).second->metaObject();
    for (int i = 0; i < metaOptions->propertyCount(); ++i) {
        const QMetaProperty property = metaOptions->property(i);
        if (qstrcmp(property.name(), "objectName") == 0) {
            continue;
        }
        support += QString::fromUtf8(property.name()) + QLatin1String(": ") + (*it).second->property(property.name()).toString() + QLatin1Char('\n');
    }

    return support;
}

bool EffectsHandlerImpl::isScreenLocked() const
{
#if KWIN_BUILD_SCREENLOCKER
    return ScreenLockerWatcher::self()->isLocked();
#else
    return false;
#endif
}

QString EffectsHandlerImpl::debug(const QString &name, const QString &parameter) const
{
    QString internalName = name.toLower();
    for (QVector<EffectPair>::const_iterator it = loaded_effects.constBegin(); it != loaded_effects.constEnd(); ++it) {
        if ((*it).first == internalName) {
            return it->second->debug(parameter);
        }
    }
    return QString();
}

bool EffectsHandlerImpl::makeOpenGLContextCurrent()
{
    return m_scene->makeOpenGLContextCurrent();
}

void EffectsHandlerImpl::doneOpenGLContextCurrent()
{
    m_scene->doneOpenGLContextCurrent();
}

bool EffectsHandlerImpl::animationsSupported() const
{
    static const QByteArray forceEnvVar = qgetenv("KWIN_EFFECTS_FORCE_ANIMATIONS");
    if (!forceEnvVar.isEmpty()) {
        static const int forceValue = forceEnvVar.toInt();
        return forceValue == 1;
    }
    return m_scene->animationsSupported();
}

void EffectsHandlerImpl::highlightWindows(const QVector<EffectWindow *> &windows)
{
    Effect *e = provides(Effect::HighlightWindows);
    if (!e) {
        return;
    }
    e->perform(Effect::HighlightWindows, QVariantList{QVariant::fromValue(windows)});
}

PlatformCursorImage EffectsHandlerImpl::cursorImage() const
{
    return kwinApp()->platform()->cursorImage();
}

void EffectsHandlerImpl::hideCursor()
{
    Cursors::self()->hideCursor();
}

void EffectsHandlerImpl::showCursor()
{
    Cursors::self()->showCursor();
}

void EffectsHandlerImpl::startInteractiveWindowSelection(std::function<void(KWin::EffectWindow *)> callback)
{
    kwinApp()->platform()->startInteractiveWindowSelection([callback](KWin::Window *window) {
        if (window && window->effectWindow()) {
            callback(window->effectWindow());
        } else {
            callback(nullptr);
        }
    });
}

void EffectsHandlerImpl::startInteractivePositionSelection(std::function<void(const QPoint &)> callback)
{
    kwinApp()->platform()->startInteractivePositionSelection(callback);
}

void EffectsHandlerImpl::showOnScreenMessage(const QString &message, const QString &iconName)
{
    OSD::show(message, iconName);
}

void EffectsHandlerImpl::hideOnScreenMessage(OnScreenMessageHideFlags flags)
{
    OSD::HideFlags osdFlags;
    if (flags.testFlag(OnScreenMessageHideFlag::SkipsCloseAnimation)) {
        osdFlags |= OSD::HideFlag::SkipCloseAnimation;
    }
    OSD::hide(osdFlags);
}

KSharedConfigPtr EffectsHandlerImpl::config() const
{
    return kwinApp()->config();
}

KSharedConfigPtr EffectsHandlerImpl::inputConfig() const
{
    return InputConfig::self()->inputConfig();
}

Effect *EffectsHandlerImpl::findEffect(const QString &name) const
{
    auto it = std::find_if(loaded_effects.constBegin(), loaded_effects.constEnd(), [name](const EffectPair &pair) {
        return pair.first == name;
    });
    if (it == loaded_effects.constEnd()) {
        return nullptr;
    }
    return (*it).second;
}

void EffectsHandlerImpl::renderOffscreenQuickView(OffscreenQuickView *w) const
{
    if (!w->isVisible()) {
        return;
    }
    scene()->paintOffscreenQuickView(w);
}

SessionState EffectsHandlerImpl::sessionState() const
{
    return Workspace::self()->sessionManager()->state();
}

QList<EffectScreen *> EffectsHandlerImpl::screens() const
{
    return m_effectScreens;
}

EffectScreen *EffectsHandlerImpl::screenAt(const QPoint &point) const
{
    return EffectScreenImpl::get(kwinApp()->platform()->outputAt(point));
}

EffectScreen *EffectsHandlerImpl::findScreen(const QString &name) const
{
    for (EffectScreen *screen : qAsConst(m_effectScreens)) {
        if (screen->name() == name) {
            return screen;
        }
    }
    return nullptr;
}

EffectScreen *EffectsHandlerImpl::findScreen(int screenId) const
{
    return m_effectScreens.value(screenId);
}

void EffectsHandlerImpl::slotOutputEnabled(Output *output)
{
    EffectScreen *screen = new EffectScreenImpl(output, this);
    m_effectScreens.append(screen);
    Q_EMIT screenAdded(screen);
}

void EffectsHandlerImpl::slotOutputDisabled(Output *output)
{
    EffectScreen *screen = EffectScreenImpl::get(output);
    m_effectScreens.removeOne(screen);
    Q_EMIT screenRemoved(screen);
    delete screen;
}

void EffectsHandlerImpl::renderScreen(EffectScreen *screen)
{
    RenderTarget renderTarget(GLFramebuffer::currentFramebuffer());
    renderTarget.setDevicePixelRatio(screen->devicePixelRatio());

    auto output = static_cast<EffectScreenImpl *>(screen)->platformOutput();
    m_scene->prePaint(output);
    m_scene->paint(&renderTarget, output->geometry());
    m_scene->postPaint();
}

bool EffectsHandlerImpl::isCursorHidden() const
{
    return Cursors::self()->isCursorHidden();
}

QRect EffectsHandlerImpl::renderTargetRect() const
{
    return m_scene->renderTargetRect();
}

qreal EffectsHandlerImpl::renderTargetScale() const
{
    return m_scene->renderTargetScale();
}

KWin::EffectWindow *EffectsHandlerImpl::inputPanel() const
{
    if (!InputMethod::self() || !InputMethod::self()->isEnabled()) {
        return nullptr;
    }

    auto panel = InputMethod::self()->panel();
    if (panel) {
        return panel->effectWindow();
    }
    return nullptr;
}

bool EffectsHandlerImpl::isInputPanelOverlay() const
{
    if (!InputMethod::self() || !InputMethod::self()->isEnabled()) {
        return true;
    }

    auto panel = InputMethod::self()->panel();
    if (panel) {
        return panel->mode() == InputPanelV1Window::Overlay;
    }
    return true;
}

//****************************************
// EffectScreenImpl
//****************************************

EffectScreenImpl::EffectScreenImpl(Output *output, QObject *parent)
    : EffectScreen(parent)
    , m_platformOutput(output)
{
    m_platformOutput->m_effectScreen = this;

    connect(output, &Output::aboutToChange, this, &EffectScreen::aboutToChange);
    connect(output, &Output::changed, this, &EffectScreen::changed);
    connect(output, &Output::wakeUp, this, &EffectScreen::wakeUp);
    connect(output, &Output::aboutToTurnOff, this, &EffectScreen::aboutToTurnOff);
    connect(output, &Output::scaleChanged, this, &EffectScreen::devicePixelRatioChanged);
    connect(output, &Output::geometryChanged, this, &EffectScreen::geometryChanged);
}

EffectScreenImpl::~EffectScreenImpl()
{
    if (m_platformOutput) {
        m_platformOutput->m_effectScreen = nullptr;
    }
}

EffectScreenImpl *EffectScreenImpl::get(Output *output)
{
    return output->m_effectScreen;
}

Output *EffectScreenImpl::platformOutput() const
{
    return m_platformOutput;
}

QString EffectScreenImpl::name() const
{
    return m_platformOutput->name();
}

qreal EffectScreenImpl::devicePixelRatio() const
{
    return m_platformOutput->scale();
}

QRect EffectScreenImpl::geometry() const
{
    return m_platformOutput->geometry();
}

int EffectScreenImpl::refreshRate() const
{
    return m_platformOutput->refreshRate();
}

EffectScreen::Transform EffectScreenImpl::transform() const
{
    return EffectScreen::Transform(m_platformOutput->transform());
}

//****************************************
// EffectWindowImpl
//****************************************

EffectWindowImpl::EffectWindowImpl(Window *window)
    : EffectWindow(window)
    , m_window(window)
    , m_windowItem(nullptr)
{
    // Deleted windows are not managed. So, when windowClosed signal is
    // emitted, effects can't distinguish managed windows from unmanaged
    // windows(e.g. combo box popups, popup menus, etc). Save value of the
    // managed property during construction of EffectWindow. At that time,
    // parent can be Client, XdgShellClient, or Unmanaged. So, later on, when
    // an instance of Deleted becomes parent of the EffectWindow, effects
    // can still figure out whether it is/was a managed window.
    managed = window->isClient();

    m_waylandWindow = qobject_cast<KWin::WaylandWindow *>(window) != nullptr;
    m_x11Window = qobject_cast<KWin::X11Window *>(window) != nullptr || qobject_cast<KWin::Unmanaged *>(window) != nullptr;
}

EffectWindowImpl::~EffectWindowImpl()
{
}

void EffectWindowImpl::refVisible(int reason)
{
    m_windowItem->refVisible(reason);
}

void EffectWindowImpl::unrefVisible(int reason)
{
    m_windowItem->unrefVisible(reason);
}

void EffectWindowImpl::addRepaint(const QRect &r)
{
    m_windowItem->scheduleRepaint(r);
}

void EffectWindowImpl::addRepaintFull()
{
    m_windowItem->scheduleRepaint(m_windowItem->boundingRect());
}

void EffectWindowImpl::addLayerRepaint(const QRect &r)
{
    m_windowItem->scheduleRepaint(m_windowItem->mapFromGlobal(r));
}

const EffectWindowGroup *EffectWindowImpl::group() const
{
    if (auto c = qobject_cast<X11Window *>(m_window)) {
        return c->group()->effectGroup();
    }
    return nullptr; // TODO
}

void EffectWindowImpl::refWindow()
{
    if (m_window->isDeleted()) {
        return m_window->ref();
    }
    Q_UNREACHABLE();
}

void EffectWindowImpl::unrefWindow()
{
    if (m_window->isDeleted()) {
        return m_window->unref();
    }
    Q_UNREACHABLE();
}

EffectScreen *EffectWindowImpl::screen() const
{
    return EffectScreenImpl::get(m_window->output());
}

#define WINDOW_HELPER(rettype, prototype, toplevelPrototype) \
    rettype EffectWindowImpl::prototype() const              \
    {                                                        \
        return m_window->toplevelPrototype();                \
    }

WINDOW_HELPER(double, opacity, opacity)
WINDOW_HELPER(bool, hasAlpha, hasAlpha)
WINDOW_HELPER(int, x, x)
WINDOW_HELPER(int, y, y)
WINDOW_HELPER(int, width, width)
WINDOW_HELPER(int, height, height)
WINDOW_HELPER(QPoint, pos, pos)
WINDOW_HELPER(QSize, size, size)
WINDOW_HELPER(QRect, geometry, frameGeometry)
WINDOW_HELPER(QRect, frameGeometry, frameGeometry)
WINDOW_HELPER(QRect, bufferGeometry, bufferGeometry)
WINDOW_HELPER(QRect, clientGeometry, clientGeometry)
WINDOW_HELPER(QRect, expandedGeometry, visibleGeometry)
WINDOW_HELPER(QRect, rect, rect)
WINDOW_HELPER(int, desktop, desktop)
WINDOW_HELPER(bool, isDesktop, isDesktop)
WINDOW_HELPER(bool, isDock, isDock)
WINDOW_HELPER(bool, isToolbar, isToolbar)
WINDOW_HELPER(bool, isMenu, isMenu)
WINDOW_HELPER(bool, isNormalWindow, isNormalWindow)
WINDOW_HELPER(bool, isDialog, isDialog)
WINDOW_HELPER(bool, isSplash, isSplash)
WINDOW_HELPER(bool, isUtility, isUtility)
WINDOW_HELPER(bool, isDropdownMenu, isDropdownMenu)
WINDOW_HELPER(bool, isPopupMenu, isPopupMenu)
WINDOW_HELPER(bool, isTooltip, isTooltip)
WINDOW_HELPER(bool, isNotification, isNotification)
WINDOW_HELPER(bool, isCriticalNotification, isCriticalNotification)
WINDOW_HELPER(bool, isAppletPopup, isAppletPopup)
WINDOW_HELPER(bool, isOnScreenDisplay, isOnScreenDisplay)
WINDOW_HELPER(bool, isComboBox, isComboBox)
WINDOW_HELPER(bool, isDNDIcon, isDNDIcon)
WINDOW_HELPER(bool, isDeleted, isDeleted)
WINDOW_HELPER(QString, windowRole, windowRole)
WINDOW_HELPER(QStringList, activities, activities)
WINDOW_HELPER(bool, skipsCloseAnimation, skipsCloseAnimation)
WINDOW_HELPER(KWaylandServer::SurfaceInterface *, surface, surface)
WINDOW_HELPER(bool, isPopupWindow, isPopupWindow)
WINDOW_HELPER(bool, isOutline, isOutline)
WINDOW_HELPER(bool, isLockScreen, isLockScreen)
WINDOW_HELPER(pid_t, pid, pid)
WINDOW_HELPER(qlonglong, windowId, window)
WINDOW_HELPER(QUuid, internalId, internalId)
WINDOW_HELPER(bool, isMinimized, isMinimized)
WINDOW_HELPER(bool, isModal, isModal)
WINDOW_HELPER(bool, isFullScreen, isFullScreen)
WINDOW_HELPER(bool, keepAbove, keepAbove)
WINDOW_HELPER(bool, keepBelow, keepBelow)
WINDOW_HELPER(QString, caption, caption);
WINDOW_HELPER(QVector<uint>, desktops, x11DesktopIds);
WINDOW_HELPER(bool, isMovable, isMovable)
WINDOW_HELPER(bool, isMovableAcrossScreens, isMovableAcrossScreens)
WINDOW_HELPER(bool, isUserMove, isInteractiveMove)
WINDOW_HELPER(bool, isUserResize, isInteractiveResize)
WINDOW_HELPER(QRect, iconGeometry, iconGeometry)
WINDOW_HELPER(bool, isSpecialWindow, isSpecialWindow)
WINDOW_HELPER(bool, acceptsFocus, wantsInput)
WINDOW_HELPER(QIcon, icon, icon)
WINDOW_HELPER(bool, isSkipSwitcher, skipSwitcher)
WINDOW_HELPER(bool, decorationHasAlpha, decorationHasAlpha)
WINDOW_HELPER(bool, isUnresponsive, unresponsive)

#undef WINDOW_HELPER

// legacy from tab groups, can be removed when no effects use this any more.
bool EffectWindowImpl::isCurrentTab() const
{
    return true;
}

QString EffectWindowImpl::windowClass() const
{
    return m_window->resourceName() + QLatin1Char(' ') + m_window->resourceClass();
}

QRect EffectWindowImpl::contentsRect() const
{
    return QRect(m_window->clientPos(), m_window->clientSize());
}

NET::WindowType EffectWindowImpl::windowType() const
{
    return m_window->windowType();
}

QSize EffectWindowImpl::basicUnit() const
{
    if (auto window = qobject_cast<X11Window *>(m_window)) {
        return window->basicUnit();
    }
    return QSize(1, 1);
}

void EffectWindowImpl::setWindow(Window *w)
{
    m_window = w;
    setParent(w);
}

void EffectWindowImpl::setWindowItem(WindowItem *item)
{
    m_windowItem = item;
}

QRect EffectWindowImpl::decorationInnerRect() const
{
    return m_window->rect() - m_window->frameMargins();
}

KDecoration2::Decoration *EffectWindowImpl::decoration() const
{
    return m_window->decoration();
}

QByteArray EffectWindowImpl::readProperty(long atom, long type, int format) const
{
    if (!kwinApp()->x11Connection()) {
        return QByteArray();
    }
    return readWindowProperty(window()->window(), atom, type, format);
}

void EffectWindowImpl::deleteProperty(long int atom) const
{
    if (kwinApp()->x11Connection()) {
        deleteWindowProperty(window()->window(), atom);
    }
}

EffectWindow *EffectWindowImpl::findModal()
{
    Window *modal = m_window->findModal();
    if (modal) {
        return modal->effectWindow();
    }

    return nullptr;
}

EffectWindow *EffectWindowImpl::transientFor()
{
    Window *transientFor = m_window->transientFor();
    if (transientFor) {
        return transientFor->effectWindow();
    }

    return nullptr;
}

QWindow *EffectWindowImpl::internalWindow() const
{
    if (auto window = qobject_cast<InternalWindow *>(m_window)) {
        return window->handle();
    }
    return nullptr;
}

EffectWindowList EffectWindowImpl::mainWindows() const
{
    const auto mainwindows = m_window->mainWindows();
    EffectWindowList ret;
    ret.reserve(mainwindows.size());
    std::transform(std::cbegin(mainwindows), std::cend(mainwindows), std::back_inserter(ret), [](auto window) {
        return window->effectWindow();
    });
    return ret;
}

void EffectWindowImpl::setData(int role, const QVariant &data)
{
    if (!data.isNull()) {
        dataMap[role] = data;
    } else {
        dataMap.remove(role);
    }
    Q_EMIT effects->windowDataChanged(this, role);
}

QVariant EffectWindowImpl::data(int role) const
{
    return dataMap.value(role);
}

void EffectWindowImpl::elevate(bool elevate)
{
    effects->setElevatedWindow(this, elevate);
}

void EffectWindowImpl::minimize()
{
    if (m_window->isClient()) {
        m_window->minimize();
    }
}

void EffectWindowImpl::unminimize()
{
    if (m_window->isClient()) {
        m_window->unminimize();
    }
}

void EffectWindowImpl::closeWindow()
{
    if (m_window->isClient()) {
        m_window->closeWindow();
    }
}

void EffectWindowImpl::referencePreviousWindowPixmap()
{
    // TODO: Implement.
}

void EffectWindowImpl::unreferencePreviousWindowPixmap()
{
    // TODO: Implement.
}

bool EffectWindowImpl::isManaged() const
{
    return managed;
}

bool EffectWindowImpl::isWaylandClient() const
{
    return m_waylandWindow;
}

bool EffectWindowImpl::isX11Client() const
{
    return m_x11Window;
}

//****************************************
// EffectWindowGroupImpl
//****************************************

EffectWindowList EffectWindowGroupImpl::members() const
{
    const auto memberList = group->members();
    EffectWindowList ret;
    ret.reserve(memberList.size());
    std::transform(std::cbegin(memberList), std::cend(memberList), std::back_inserter(ret), [](auto window) {
        return window->effectWindow();
    });
    return ret;
}

//****************************************
// EffectFrameImpl
//****************************************

EffectFrameQuickScene::EffectFrameQuickScene(EffectFrameStyle style, bool staticSize, QPoint position,
                                             Qt::Alignment alignment, QObject *parent)
    : OffscreenQuickScene(parent)
    , m_style(style)
    , m_static(staticSize)
    , m_point(position)
    , m_alignment(alignment)
{

    QString name;
    switch (style) {
    case EffectFrameNone:
        name = QStringLiteral("none");
        break;
    case EffectFrameUnstyled:
        name = QStringLiteral("unstyled");
        break;
    case EffectFrameStyled:
        name = QStringLiteral("styled");
        break;
    }

    const QString defaultPath = QStringLiteral(KWIN_NAME "/frames/plasma/frame_%1.qml").arg(name);
    // TODO read from kwinApp()->config() "QmlPath" like Outline/OnScreenNotification
    // *if* someone really needs this to be configurable.
    const QString path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, defaultPath);

    setSource(QUrl::fromLocalFile(path), QVariantMap{{QStringLiteral("effectFrame"), QVariant::fromValue(this)}});

    if (rootItem()) {
        connect(rootItem(), &QQuickItem::implicitWidthChanged, this, &EffectFrameQuickScene::reposition);
        connect(rootItem(), &QQuickItem::implicitHeightChanged, this, &EffectFrameQuickScene::reposition);
    }
}

EffectFrameQuickScene::~EffectFrameQuickScene() = default;

EffectFrameStyle EffectFrameQuickScene::style() const
{
    return m_style;
}

bool EffectFrameQuickScene::isStatic() const
{
    return m_static;
}

const QFont &EffectFrameQuickScene::font() const
{
    return m_font;
}

void EffectFrameQuickScene::setFont(const QFont &font)
{
    if (m_font == font) {
        return;
    }

    m_font = font;
    Q_EMIT fontChanged(font);
    reposition();
}

const QIcon &EffectFrameQuickScene::icon() const
{
    return m_icon;
}

void EffectFrameQuickScene::setIcon(const QIcon &icon)
{
    m_icon = icon;
    Q_EMIT iconChanged(icon);
    reposition();
}

const QSize &EffectFrameQuickScene::iconSize() const
{
    return m_iconSize;
}

void EffectFrameQuickScene::setIconSize(const QSize &iconSize)
{
    if (m_iconSize == iconSize) {
        return;
    }

    m_iconSize = iconSize;
    Q_EMIT iconSizeChanged(iconSize);
    reposition();
}

const QString &EffectFrameQuickScene::text() const
{
    return m_text;
}

void EffectFrameQuickScene::setText(const QString &text)
{
    if (m_text == text) {
        return;
    }

    m_text = text;
    Q_EMIT textChanged(text);
    reposition();
}

qreal EffectFrameQuickScene::frameOpacity() const
{
    return m_frameOpacity;
}

void EffectFrameQuickScene::setFrameOpacity(qreal frameOpacity)
{
    if (m_frameOpacity != frameOpacity) {
        m_frameOpacity = frameOpacity;
        Q_EMIT frameOpacityChanged(frameOpacity);
    }
}

bool EffectFrameQuickScene::crossFadeEnabled() const
{
    return m_crossFadeEnabled;
}

void EffectFrameQuickScene::setCrossFadeEnabled(bool enabled)
{
    if (m_crossFadeEnabled != enabled) {
        m_crossFadeEnabled = enabled;
        Q_EMIT crossFadeEnabledChanged(enabled);
    }
}

qreal EffectFrameQuickScene::crossFadeProgress() const
{
    return m_crossFadeProgress;
}

void EffectFrameQuickScene::setCrossFadeProgress(qreal progress)
{
    if (m_crossFadeProgress != progress) {
        m_crossFadeProgress = progress;
        Q_EMIT crossFadeProgressChanged(progress);
    }
}

Qt::Alignment EffectFrameQuickScene::alignment() const
{
    return m_alignment;
}

void EffectFrameQuickScene::setAlignment(Qt::Alignment alignment)
{
    if (m_alignment == alignment) {
        return;
    }

    m_alignment = alignment;
    reposition();
}

QPoint EffectFrameQuickScene::position() const
{
    return m_point;
}

void EffectFrameQuickScene::setPosition(const QPoint &point)
{
    if (m_point == point) {
        return;
    }

    m_point = point;
    reposition();
}

void EffectFrameQuickScene::reposition()
{
    if (!rootItem() || m_point.x() < 0 || m_point.y() < 0) {
        return;
    }

    QSizeF size;
    if (m_static) {
        size = rootItem()->size();
    } else {
        size = QSizeF(rootItem()->implicitWidth(), rootItem()->implicitHeight());
    }

    QRect geometry(QPoint(), size.toSize());

    if (m_alignment & Qt::AlignLeft)
        geometry.moveLeft(m_point.x());
    else if (m_alignment & Qt::AlignRight)
        geometry.moveLeft(m_point.x() - geometry.width());
    else
        geometry.moveLeft(m_point.x() - geometry.width() / 2);
    if (m_alignment & Qt::AlignTop)
        geometry.moveTop(m_point.y());
    else if (m_alignment & Qt::AlignBottom)
        geometry.moveTop(m_point.y() - geometry.height());
    else
        geometry.moveTop(m_point.y() - geometry.height() / 2);

    if (geometry == this->geometry()) {
        return;
    }

    setGeometry(geometry);
}

EffectFrameImpl::EffectFrameImpl(EffectFrameStyle style, bool staticSize, QPoint position, Qt::Alignment alignment)
    : QObject(nullptr)
    , EffectFrame()
    , m_view(new EffectFrameQuickScene(style, staticSize, position, alignment, nullptr))
{
    connect(m_view, &OffscreenQuickScene::repaintNeeded, this, [this] {
        effects->addRepaint(geometry());
    });
    connect(m_view, &OffscreenQuickScene::geometryChanged, this, [this](const QRect &oldGeometry, const QRect &newGeometry) {
        effects->addRepaint(oldGeometry);
        m_geometry = newGeometry;
        effects->addRepaint(newGeometry);
    });
}

EffectFrameImpl::~EffectFrameImpl()
{
    // Effects often destroy their cached TextFrames in pre/postPaintScreen.
    // Destroying an OffscreenQuickView changes GL context, which we
    // must not do during effect rendering.
    // Delay destruction of the view until after the rendering.
    m_view->deleteLater();
}

Qt::Alignment EffectFrameImpl::alignment() const
{
    return m_view->alignment();
}

void EffectFrameImpl::setAlignment(Qt::Alignment alignment)
{
    m_view->setAlignment(alignment);
}

const QFont &EffectFrameImpl::font() const
{
    return m_view->font();
}

void EffectFrameImpl::setFont(const QFont &font)
{
    m_view->setFont(font);
}

void EffectFrameImpl::free()
{
    m_view->hide();
}

const QRect &EffectFrameImpl::geometry() const
{
    // Can't forward to OffscreenQuickScene::geometry() because we return a reference.
    return m_geometry;
}

void EffectFrameImpl::setGeometry(const QRect &geometry, bool force)
{
    Q_UNUSED(force)
    m_view->setGeometry(geometry);
}

const QIcon &EffectFrameImpl::icon() const
{
    return m_view->icon();
}

void EffectFrameImpl::setIcon(const QIcon &icon)
{
    m_view->setIcon(icon);

    if (m_view->iconSize().isEmpty() && !icon.availableSizes().isEmpty()) { // Set a size if we don't already have one
        setIconSize(icon.availableSizes().constFirst());
    }
}

const QSize &EffectFrameImpl::iconSize() const
{
    return m_view->iconSize();
}

void EffectFrameImpl::setIconSize(const QSize &size)
{
    m_view->setIconSize(size);
}

void EffectFrameImpl::setPosition(const QPoint &point)
{
    m_view->setPosition(point);
}

void EffectFrameImpl::render(const QRegion &region, double opacity, double frameOpacity)
{
    Q_UNUSED(region);

    if (!m_view->rootItem()) {
        return;
    }

    m_view->show();

    m_view->setOpacity(opacity);
    m_view->setFrameOpacity(frameOpacity);

    effects->renderOffscreenQuickView(m_view);
}

const QString &EffectFrameImpl::text() const
{
    return m_view->text();
}

void EffectFrameImpl::setText(const QString &text)
{
    m_view->setText(text);
}

EffectFrameStyle EffectFrameImpl::style() const
{
    return m_view->style();
}

bool EffectFrameImpl::isCrossFade() const
{
    return m_view->crossFadeEnabled();
}

void EffectFrameImpl::enableCrossFade(bool enable)
{
    m_view->setCrossFadeEnabled(enable);
}

qreal EffectFrameImpl::crossFadeProgress() const
{
    return m_view->crossFadeProgress();
}

void EffectFrameImpl::setCrossFadeProgress(qreal progress)
{
    m_view->setCrossFadeProgress(progress);
}

} // namespace
