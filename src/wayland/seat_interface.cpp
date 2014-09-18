/********************************************************************
Copyright 2014  Martin Gräßlin <mgraesslin@kde.org>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) version 3, or any
later version accepted by the membership of KDE e.V. (or its
successor approved by the membership of KDE e.V.), which shall
act as a proxy defined in Section 6 of version 3 of the license.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "seat_interface.h"
#include "display.h"
#include "surface_interface.h"

#include <fcntl.h>
#include <unistd.h>

#ifndef WL_SEAT_NAME_SINCE_VERSION
#define WL_SEAT_NAME_SINCE_VERSION 2
#endif

namespace KWayland
{

namespace Server
{

static const quint32 s_version = 3;

class SeatInterface::Private
{
public:
    Private(SeatInterface *q, Display *d);
    void create();
    void bind(wl_client *client, uint32_t version, uint32_t id);
    void sendCapabilities(wl_resource *r);
    void sendName(wl_resource *r);

    Display *display;
    wl_global *seat = nullptr;
    QString name;
    bool pointer = false;
    bool keyboard = false;
    bool touch = false;
    QList<wl_resource*> resources;
    PointerInterface *pointerInterface;
    KeyboardInterface *keyboardInterface;

private:
    static Private *cast(wl_resource *r);
    static void bind(wl_client *client, void *data, uint32_t version, uint32_t id);
    static void unbind(wl_resource *r);

    // interface
    static void getPointerCallback(wl_client *client, wl_resource *resource, uint32_t id);
    static void getKeyboardCallback(wl_client *client, wl_resource *resource, uint32_t id);
    static void getTouchCallback(wl_client *client, wl_resource *resource, uint32_t id);
    static const struct wl_seat_interface s_interface;
};

SeatInterface::Private::Private(SeatInterface *q, Display *d)
    : display(d)
    , pointerInterface(new PointerInterface(d, q))
    , keyboardInterface(new KeyboardInterface(d, q))
{
}

void SeatInterface::Private::create()
{
    Q_ASSERT(!seat);
    seat = wl_global_create(*display, &wl_seat_interface, s_version, this, &bind);
}

const struct wl_seat_interface SeatInterface::Private::s_interface = {
    getPointerCallback,
    getKeyboardCallback,
    getTouchCallback
};

SeatInterface::SeatInterface(Display *display, QObject *parent)
    : QObject(parent)
    , d(new Private(this, display))
{
    connect(this, &SeatInterface::nameChanged, this,
        [this] {
            for (auto it = d->resources.constBegin(); it != d->resources.constEnd(); ++it) {
                d->sendName(*it);
            }
        }
    );
    auto sendCapabilitiesAll = [this] {
        for (auto it = d->resources.constBegin(); it != d->resources.constEnd(); ++it) {
            d->sendCapabilities(*it);
        }
    };
    connect(this, &SeatInterface::hasPointerChanged,  this, sendCapabilitiesAll);
    connect(this, &SeatInterface::hasKeyboardChanged, this, sendCapabilitiesAll);
    connect(this, &SeatInterface::hasTouchChanged,    this, sendCapabilitiesAll);
}

SeatInterface::~SeatInterface()
{
    destroy();
}

void SeatInterface::create()
{
    d->create();
}

void SeatInterface::destroy()
{
    while (!d->resources.isEmpty()) {
        wl_resource_destroy(d->resources.takeLast());
    }
    if (d->seat) {
        wl_global_destroy(d->seat);
        d->seat = nullptr;
    }
}

void SeatInterface::Private::bind(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    reinterpret_cast<SeatInterface::Private*>(data)->bind(client, version, id);
}

void SeatInterface::Private::bind(wl_client *client, uint32_t version, uint32_t id)
{
    wl_resource *r = wl_resource_create(client, &wl_seat_interface, qMin(s_version, version), id);
    if (!r) {
        wl_client_post_no_memory(client);
        return;
    }
    resources << r;

    wl_resource_set_implementation(r, &s_interface, this, unbind);

    sendCapabilities(r);
    sendName(r);
}

void SeatInterface::Private::unbind(wl_resource *r)
{
    cast(r)->resources.removeAll(r);
}

void SeatInterface::Private::sendName(wl_resource *r)
{
    if (wl_resource_get_version(r) < WL_SEAT_NAME_SINCE_VERSION) {
        return;
    }
    wl_seat_send_name(r, name.toUtf8().constData());
}

void SeatInterface::Private::sendCapabilities(wl_resource *r)
{
    uint32_t capabilities = 0;
    if (pointer) {
        capabilities |= WL_SEAT_CAPABILITY_POINTER;
    }
    if (keyboard) {
        capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    if (touch) {
        capabilities |= WL_SEAT_CAPABILITY_TOUCH;
    }
    wl_seat_send_capabilities(r, capabilities);
}

SeatInterface::Private *SeatInterface::Private::cast(wl_resource *r)
{
    return reinterpret_cast<SeatInterface::Private*>(wl_resource_get_user_data(r));
}

void SeatInterface::setHasKeyboard(bool has)
{
    if (d->keyboard == has) {
        return;
    }
    d->keyboard = has;
    emit hasKeyboardChanged(d->keyboard);
}

void SeatInterface::setHasPointer(bool has)
{
    if (d->pointer == has) {
        return;
    }
    d->pointer = has;
    emit hasPointerChanged(d->pointer);
}

void SeatInterface::setHasTouch(bool has)
{
    if (d->touch == has) {
        return;
    }
    d->touch = has;
    emit hasTouchChanged(d->touch);
}

void SeatInterface::setName(const QString &name)
{
    if (d->name == name) {
        return;
    }
    d->name = name;
    emit nameChanged(d->name);
}

void SeatInterface::Private::getPointerCallback(wl_client *client, wl_resource *resource, uint32_t id)
{
    cast(resource)->pointerInterface->createInterface(client, resource, id);
}

void SeatInterface::Private::getKeyboardCallback(wl_client *client, wl_resource *resource, uint32_t id)
{
    cast(resource)->keyboardInterface->createInterfae(client, resource, id);
}

void SeatInterface::Private::getTouchCallback(wl_client *client, wl_resource *resource, uint32_t id)
{
    Q_UNUSED(client)
    Q_UNUSED(resource)
    Q_UNUSED(id)
}

bool SeatInterface::isValid() const {
    return d->seat != nullptr;
}

QString SeatInterface::name() const
{
    return d->name;
}

bool SeatInterface::hasPointer() const
{
    return d->pointer;
}

bool SeatInterface::hasKeyboard() const
{
    return d->keyboard;
}

bool SeatInterface::hasTouch() const
{
    return d->touch;
}

PointerInterface *SeatInterface::pointer()
{
    return d->pointerInterface;
}

KeyboardInterface *SeatInterface::keyboard()
{
    return d->keyboardInterface;
}

/****************************************
 * PointerInterface
 ***************************************/

const struct wl_pointer_interface PointerInterface::s_interface = {
    PointerInterface::setCursorCallback,
    PointerInterface::releaseCallback
};

PointerInterface::PointerInterface(Display *display, SeatInterface *parent)
    : QObject(parent)
    , m_display(display)
    , m_seat(parent)
    , m_eventTime(0)
{
}

PointerInterface::~PointerInterface()
{
    while (!m_resources.isEmpty()) {
        ResourceData data = m_resources.takeLast();
        wl_resource_destroy(data.pointer);
    }
}

void PointerInterface::createInterface(wl_client *client, wl_resource *parentResource, uint32_t id)
{
    wl_resource *p = wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(parentResource), id);
    if (!p) {
        wl_resource_post_no_memory(parentResource);
        return;
    }
    ResourceData data;
    data.client = client;
    data.pointer = p;
    m_resources << data;

    wl_resource_set_implementation(p, &PointerInterface::s_interface, this, PointerInterface::unbind);
}

wl_resource *PointerInterface::pointerForSurface(SurfaceInterface *surface) const
{
    if (!surface) {
        return nullptr;
    }
    for (auto it = m_resources.constBegin(); it != m_resources.constEnd(); ++it) {
        if ((*it).client == surface->client()) {
            return (*it).pointer;
        }
    }
    return nullptr;
}

void PointerInterface::setFocusedSurface(SurfaceInterface *surface, const QPoint &surfacePosition)
{
    const quint32 serial = m_display->nextSerial();
    if (m_focusedSurface.surface && m_focusedSurface.pointer) {
        wl_pointer_send_leave(m_focusedSurface.pointer, serial, m_focusedSurface.surface->surface());
        disconnect(m_focusedSurface.surface, &QObject::destroyed, this, &PointerInterface::surfaceDeleted);
    }
    m_focusedSurface.pointer = pointerForSurface(surface);
    if (!m_focusedSurface.pointer) {
        m_focusedSurface = FocusedSurface();
        return;
    }
    m_focusedSurface.surface = surface;
    m_focusedSurface.offset = surfacePosition;
    m_focusedSurface.serial = serial;
    connect(m_focusedSurface.surface, &QObject::destroyed, this, &PointerInterface::surfaceDeleted);

    const QPoint pos = m_globalPos - surfacePosition;
    wl_pointer_send_enter(m_focusedSurface.pointer, m_focusedSurface.serial,
                          m_focusedSurface.surface->surface(),
                          wl_fixed_from_int(pos.x()), wl_fixed_from_int(pos.y()));
}

void PointerInterface::surfaceDeleted()
{
    m_focusedSurface = FocusedSurface();
}

void PointerInterface::setFocusedSurfacePosition(const QPoint &surfacePosition)
{
    if (!m_focusedSurface.surface) {
        return;
    }
    m_focusedSurface.offset = surfacePosition;
}

void PointerInterface::setGlobalPos(const QPoint &pos)
{
    if (m_globalPos == pos) {
        return;
    }
    m_globalPos = pos;
    if (m_focusedSurface.surface && m_focusedSurface.pointer) {
        const QPoint pos = m_globalPos - m_focusedSurface.offset;
        wl_pointer_send_motion(m_focusedSurface.pointer, m_eventTime,
                               wl_fixed_from_int(pos.x()), wl_fixed_from_int(pos.y()));
    }
    emit globalPosChanged(m_globalPos);
}

void PointerInterface::updateTimestamp(quint32 time)
{
    m_eventTime = time;
}

void PointerInterface::buttonPressed(quint32 button)
{
    const quint32 serial = m_display->nextSerial();
    updateButtonSerial(button, serial);
    updateButtonState(button, ButtonState::Pressed);
    if (!m_focusedSurface.surface || !m_focusedSurface.pointer) {
        return;
    }
    wl_pointer_send_button(m_focusedSurface.pointer, serial, m_eventTime, button, WL_POINTER_BUTTON_STATE_PRESSED);
}

void PointerInterface::buttonReleased(quint32 button)
{
    const quint32 serial = m_display->nextSerial();
    updateButtonSerial(button, serial);
    updateButtonState(button, ButtonState::Released);
    if (!m_focusedSurface.surface || !m_focusedSurface.pointer) {
        return;
    }
    wl_pointer_send_button(m_focusedSurface.pointer, serial, m_eventTime, button, WL_POINTER_BUTTON_STATE_RELEASED);
}

void PointerInterface::updateButtonSerial(quint32 button, quint32 serial)
{
    auto it = m_buttonSerials.find(button);
    if (it == m_buttonSerials.end()) {
        m_buttonSerials.insert(button, serial);
        return;
    }
    it.value() = serial;
}

quint32 PointerInterface::buttonSerial(quint32 button) const
{
    auto it = m_buttonSerials.constFind(button);
    if (it == m_buttonSerials.constEnd()) {
        return 0;
    }
    return it.value();
}

void PointerInterface::updateButtonState(quint32 button, PointerInterface::ButtonState state)
{
    auto it = m_buttonStates.find(button);
    if (it == m_buttonStates.end()) {
        m_buttonStates.insert(button, state);
        return;
    }
    it.value() = state;
}

bool PointerInterface::isButtonPressed(quint32 button) const
{
    auto it = m_buttonStates.constFind(button);
    if (it == m_buttonStates.constEnd()) {
        return false;
    }
    return it.value() == ButtonState::Pressed ? true : false;
}

void PointerInterface::axis(Qt::Orientation orientation, quint32 delta)
{
    if (!m_focusedSurface.surface || !m_focusedSurface.pointer) {
        return;
    }
    wl_pointer_send_axis(m_focusedSurface.pointer, m_eventTime,
                         (orientation == Qt::Vertical) ? WL_POINTER_AXIS_VERTICAL_SCROLL : WL_POINTER_AXIS_HORIZONTAL_SCROLL,
                         wl_fixed_from_int(delta));
}

void PointerInterface::unbind(wl_resource *resource)
{
    PointerInterface *p = PointerInterface::cast(resource);
    auto it = std::find_if(p->m_resources.begin(), p->m_resources.end(),
        [resource](const ResourceData &data) {
            return data.pointer == resource;
        }
    );
    if (it == p->m_resources.end()) {
        return;
    }
    if ((*it).pointer == p->m_focusedSurface.pointer) {
        disconnect(p->m_focusedSurface.surface, &QObject::destroyed, p, &PointerInterface::surfaceDeleted);
        p->m_focusedSurface = FocusedSurface();
    }
    p->m_resources.erase(it);
}

void PointerInterface::setCursorCallback(wl_client *client, wl_resource *resource, uint32_t serial,
                                         wl_resource *surface, int32_t hotspot_x, int32_t hotspot_y)
{
    Q_UNUSED(client)
    Q_UNUSED(resource)
    Q_UNUSED(serial)
    Q_UNUSED(surface)
    Q_UNUSED(hotspot_x)
    Q_UNUSED(hotspot_y)
    // TODO: implement
}

void PointerInterface::releaseCallback(wl_client *client, wl_resource *resource)
{
    Q_UNUSED(client)
    unbind(resource);
}

/****************************************
 * KeyboardInterface
 ***************************************/

const struct wl_keyboard_interface KeyboardInterface::s_interface {
    KeyboardInterface::releaseCallback
};

KeyboardInterface::KeyboardInterface(Display *display, SeatInterface *parent)
    : QObject(parent)
    , m_display(display)
    , m_seat(parent)
    , m_eventTime(0)
{
}

KeyboardInterface::~KeyboardInterface()
{
    while (!m_resources.isEmpty()) {
        ResourceData data = m_resources.takeLast();
        wl_resource_destroy(data.keyboard);
    }
}

void KeyboardInterface::createInterfae(wl_client *client, wl_resource *parentResource, uint32_t id)
{
    wl_resource *k = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(parentResource), id);
    if (!k) {
        wl_resource_post_no_memory(parentResource);
        return;
    }
    ResourceData data;
    data.client = client;
    data.keyboard = k;
    m_resources << data;

    wl_resource_set_implementation(k, &KeyboardInterface::s_interface, this, KeyboardInterface::unbind);

    sendKeymap(k);
}

void KeyboardInterface::unbind(wl_resource *resource)
{
    KeyboardInterface *k = KeyboardInterface::cast(resource);
    auto it = std::find_if(k->m_resources.begin(), k->m_resources.end(),
        [resource](const ResourceData &data) {
            return data.keyboard == resource;
        }
    );
    if (it == k->m_resources.end()) {
        return;
    }
    if ((*it).keyboard == k->m_focusedSurface.keyboard) {
        disconnect(k->m_focusedSurface.surface, &QObject::destroyed, k, &KeyboardInterface::surfaceDeleted);
        k->m_focusedSurface = FocusedSurface();
    }
    k->m_resources.erase(it);
}

void KeyboardInterface::surfaceDeleted()
{
    m_focusedSurface = FocusedSurface();
}

void KeyboardInterface::releaseCallback(wl_client *client, wl_resource *resource)
{
    Q_UNUSED(client)
    unbind(resource);
}

void KeyboardInterface::setKeymap(int fd, quint32 size)
{
    m_keymap.xkbcommonCompatible = true;
    m_keymap.fd = fd;
    m_keymap.size = size;
    sendKeymapToAll();
}

void KeyboardInterface::sendKeymap(wl_resource *r)
{
    if (m_keymap.xkbcommonCompatible) {
        wl_keyboard_send_keymap(r,
                                WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                                m_keymap.fd,
                                m_keymap.size);
    } else {
        int nullFd = open("/dev/null", O_RDONLY);
        wl_keyboard_send_keymap(r, WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, nullFd, 0);
        close(nullFd);
    }
}

void KeyboardInterface::sendKeymapToAll()
{
    for (auto it = m_resources.constBegin(); it != m_resources.constEnd(); ++it) {
        sendKeymap((*it).keyboard);
    }
}

void KeyboardInterface::sendModifiers(wl_resource* r)
{
    wl_keyboard_send_modifiers(r, m_display->nextSerial(), m_modifiers.depressed, m_modifiers.latched, m_modifiers.locked, m_modifiers.group);
}

void KeyboardInterface::setFocusedSurface(SurfaceInterface *surface)
{
    const quint32 serial = m_display->nextSerial();
    if (m_focusedSurface.surface && m_focusedSurface.keyboard) {
        wl_keyboard_send_leave(m_focusedSurface.keyboard, serial, m_focusedSurface.surface->surface());
        disconnect(m_focusedSurface.surface, &QObject::destroyed, this, &KeyboardInterface::surfaceDeleted);
    }
    m_focusedSurface.keyboard = keyboardForSurface(surface);
    if (!m_focusedSurface.keyboard) {
        m_focusedSurface = FocusedSurface();
        return;
    }
    m_focusedSurface.surface = surface;
    connect(m_focusedSurface.surface, &QObject::destroyed, this, &KeyboardInterface::surfaceDeleted);

    wl_array keys;
    wl_array_init(&keys);
    for (auto it = m_keyStates.constBegin(); it != m_keyStates.constEnd(); ++it) {
        if (it.value() == KeyState::Pressed) {
            continue;
        }
        uint32_t *k = reinterpret_cast<uint32_t*>(wl_array_add(&keys, sizeof(uint32_t)));
        *k = it.key();
    }
    wl_keyboard_send_enter(m_focusedSurface.keyboard, serial, m_focusedSurface.surface->surface(), &keys);
    wl_array_release(&keys);

    sendModifiers(m_focusedSurface.keyboard);
}

wl_resource *KeyboardInterface::keyboardForSurface(SurfaceInterface *surface) const
{
    if (!surface) {
        return nullptr;
    }
    for (auto it = m_resources.constBegin(); it != m_resources.constEnd(); ++it) {
        if ((*it).client == surface->client()) {
            return (*it).keyboard;
        }
    }
    return nullptr;
}

void KeyboardInterface::keyPressed(quint32 key)
{
    updateKey(key, KeyState::Pressed);
    if (m_focusedSurface.surface && m_focusedSurface.keyboard) {
        wl_keyboard_send_key(m_focusedSurface.keyboard, m_display->nextSerial(), m_eventTime, key, WL_KEYBOARD_KEY_STATE_PRESSED);
    }
}

void KeyboardInterface::keyReleased(quint32 key)
{
    updateKey(key, KeyState::Released);
    if (m_focusedSurface.surface && m_focusedSurface.keyboard) {
        wl_keyboard_send_key(m_focusedSurface.keyboard, m_display->nextSerial(), m_eventTime, key, WL_KEYBOARD_KEY_STATE_RELEASED);
    }
}

void KeyboardInterface::updateKey(quint32 key, KeyboardInterface::KeyState state)
{
    auto it = m_keyStates.find(key);
    if (it == m_keyStates.end()) {
        m_keyStates.insert(key, state);
        return;
    }
    it.value() = state;
}

void KeyboardInterface::updateTimestamp(quint32 time)
{
    m_eventTime = time;
}

void KeyboardInterface::updateModifiers(quint32 depressed, quint32 latched, quint32 locked, quint32 group)
{
    m_modifiers.depressed = depressed;
    m_modifiers.latched = latched;
    m_modifiers.locked = locked;
    m_modifiers.group = group;
    if (m_focusedSurface.surface && m_focusedSurface.keyboard) {
        sendModifiers(m_focusedSurface.keyboard);
    }
}

}
}
