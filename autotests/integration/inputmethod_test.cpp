/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2020 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "abstract_client.h"
#include "abstract_output.h"
#include "cursor.h"
#include "deleted.h"
#include "effects.h"
#include "inputmethod.h"
#include "kwin_wayland_test.h"
#include "platform.h"
#include "qwayland-input-method-unstable-v1.h"
#include "qwayland-text-input-unstable-v3.h"
#include "screens.h"
#include "virtualkeyboard_dbus.h"
#include "wayland_server.h"
#include "workspace.h"

#include <KWaylandServer/clientconnection.h>
#include <KWaylandServer/display.h>
#include <KWaylandServer/seat_interface.h>
#include <KWaylandServer/surface_interface.h>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingReply>
#include <QSignalSpy>
#include <QTest>

#include <KWayland/Client/compositor.h>
#include <KWayland/Client/output.h>
#include <KWayland/Client/region.h>
#include <KWayland/Client/seat.h>
#include <KWayland/Client/surface.h>
#include <KWayland/Client/textinput.h>

using namespace KWin;
using namespace KWayland::Client;
using KWin::VirtualKeyboardDBus;

static const QString s_socketName = QStringLiteral("wayland_test_kwin_inputmethod-0");

class InputMethodTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void testOpenClose();
    void testEnableDisableV3();
    void testEnableActive();
    void testHidePanel();
    void testSwitchFocusedSurfaces();
};

void InputMethodTest::initTestCase()
{
    QDBusConnection::sessionBus().registerService(QStringLiteral("org.kde.kwin.testvirtualkeyboard"));

    qRegisterMetaType<KWin::Deleted *>();
    qRegisterMetaType<KWin::AbstractClient *>();
    qRegisterMetaType<KWayland::Client::Output *>();

    QSignalSpy applicationStartedSpy(kwinApp(), &Application::started);
    QVERIFY(applicationStartedSpy.isValid());
    kwinApp()->platform()->setInitialWindowSize(QSize(1280, 1024));
    QVERIFY(waylandServer()->init(s_socketName));
    QMetaObject::invokeMethod(kwinApp()->platform(), "setVirtualOutputs", Qt::DirectConnection, Q_ARG(int, 2));

    static_cast<WaylandTestApplication *>(kwinApp())->setInputMethodServerToStart("internal");
    kwinApp()->start();
    QVERIFY(applicationStartedSpy.wait());
    const auto outputs = kwinApp()->platform()->enabledOutputs();
    QCOMPARE(outputs.count(), 2);
    QCOMPARE(outputs[0]->geometry(), QRect(0, 0, 1280, 1024));
    QCOMPARE(outputs[1]->geometry(), QRect(1280, 0, 1280, 1024));
    Test::initWaylandWorkspace();
}

void InputMethodTest::init()
{
    QVERIFY(Test::setupWaylandConnection(Test::AdditionalWaylandInterface::Seat | Test::AdditionalWaylandInterface::TextInputManagerV2
                                         | Test::AdditionalWaylandInterface::InputMethodV1 | Test::AdditionalWaylandInterface::TextInputManagerV3));

    workspace()->setActiveOutput(QPoint(640, 512));
    KWin::Cursors::self()->mouse()->setPos(QPoint(640, 512));

    InputMethod::self()->setEnabled(true);
}

void InputMethodTest::cleanup()
{
    Test::destroyWaylandConnection();
}

void InputMethodTest::testOpenClose()
{
    QSignalSpy clientAddedSpy(workspace(), &Workspace::clientAdded);
    QSignalSpy clientRemovedSpy(workspace(), &Workspace::clientRemoved);
    QVERIFY(clientAddedSpy.isValid());

    // Create an xdg_toplevel surface and wait for the compositor to catch up.
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.data()));
    AbstractClient *client = Test::renderAndWaitForShown(surface.data(), QSize(1280, 1024), Qt::red);
    QVERIFY(client);
    QVERIFY(client->isActive());
    QCOMPARE(client->frameGeometry().size(), QSize(1280, 1024));
    QSignalSpy frameGeometryChangedSpy(client, &AbstractClient::frameGeometryChanged);
    QVERIFY(frameGeometryChangedSpy.isValid());
    QSignalSpy toplevelConfigureRequestedSpy(shellSurface.data(), &Test::XdgToplevel::configureRequested);
    QSignalSpy surfaceConfigureRequestedSpy(shellSurface->xdgSurface(), &Test::XdgSurface::configureRequested);

    QScopedPointer<TextInput> textInput(Test::waylandTextInputManager()->createTextInput(Test::waylandSeat()));

    QVERIFY(!textInput.isNull());
    textInput->enable(surface.data());
    QVERIFY(surfaceConfigureRequestedSpy.wait());

    // Show the keyboard
    textInput->showInputPanel();
    QVERIFY(clientAddedSpy.wait());

    AbstractClient *keyboardClient = clientAddedSpy.last().first().value<AbstractClient *>();
    QVERIFY(keyboardClient);
    QVERIFY(keyboardClient->isInputMethod());

    // Do the actual resize
    QVERIFY(surfaceConfigureRequestedSpy.wait());

    Test::render(surface.data(), toplevelConfigureRequestedSpy.last().first().value<QSize>(), Qt::red);
    QVERIFY(frameGeometryChangedSpy.wait());

    QCOMPARE(client->frameGeometry().height(), 1024 - keyboardClient->inputGeometry().height() + 1);

    // Hide the keyboard
    textInput->hideInputPanel();

    QVERIFY(surfaceConfigureRequestedSpy.wait());
    Test::render(surface.data(), toplevelConfigureRequestedSpy.last().first().value<QSize>(), Qt::red);
    QVERIFY(frameGeometryChangedSpy.wait());

    QCOMPARE(client->frameGeometry().height(), 1024);

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}

void InputMethodTest::testEnableDisableV3()
{
    // Create an xdg_toplevel surface and wait for the compositor to catch up.
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.data()));
    AbstractClient *client = Test::renderAndWaitForShown(surface.data(), QSize(1280, 1024), Qt::red);
    QVERIFY(client);
    QVERIFY(client->isActive());
    QCOMPARE(client->frameGeometry().size(), QSize(1280, 1024));

    Test::TextInputV3 *textInputV3 = new Test::TextInputV3();
    textInputV3->init(Test::waylandTextInputManagerV3()->get_text_input(*(Test::waylandSeat())));
    textInputV3->enable();

    QSignalSpy inputMethodActiveSpy(InputMethod::self(), &InputMethod::activeChanged);
    // just enabling the text-input should not show it but rather on commit
    QVERIFY(!InputMethod::self()->isActive());
    textInputV3->commit();
    QVERIFY(inputMethodActiveSpy.count() || inputMethodActiveSpy.wait());
    QVERIFY(InputMethod::self()->isActive());

    // disable text input and ensure that it is not hiding input panel without commit
    inputMethodActiveSpy.clear();
    QVERIFY(InputMethod::self()->isActive());
    textInputV3->disable();
    textInputV3->commit();
    QVERIFY(inputMethodActiveSpy.count() || inputMethodActiveSpy.wait());
    QVERIFY(!InputMethod::self()->isActive());
}

void InputMethodTest::testEnableActive()
{
    QVERIFY(!InputMethod::self()->isActive());

    QSignalSpy clientAddedSpy(workspace(), &Workspace::clientAdded);
    QSignalSpy clientRemovedSpy(workspace(), &Workspace::clientRemoved);

    QSignalSpy activateSpy(InputMethod::self(), &InputMethod::activeChanged);

    // Create an xdg_toplevel surface and wait for the compositor to catch up.
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.data()));
    AbstractClient *client = Test::renderAndWaitForShown(surface.data(), QSize(1280, 1024), Qt::red);
    QVERIFY(client);
    QVERIFY(client->isActive());
    QCOMPARE(client->frameGeometry().size(), QSize(1280, 1024));
    QSignalSpy frameGeometryChangedSpy(client, &AbstractClient::frameGeometryChanged);
    QVERIFY(frameGeometryChangedSpy.isValid());
    QSignalSpy toplevelConfigureRequestedSpy(shellSurface.data(), &Test::XdgToplevel::configureRequested);
    QSignalSpy surfaceConfigureRequestedSpy(shellSurface->xdgSurface(), &Test::XdgSurface::configureRequested);

    QScopedPointer<TextInput> textInput(Test::waylandTextInputManager()->createTextInput(Test::waylandSeat()));

    QVERIFY(!textInput.isNull());
    textInput->enable(surface.data());
    QVERIFY(surfaceConfigureRequestedSpy.wait());
    QCOMPARE(clientAddedSpy.count(), 1);

    // Show the keyboard
    textInput->showInputPanel();
    QVERIFY(clientAddedSpy.wait());

    QCOMPARE(workspace()->activeClient(), client);

    activateSpy.clear();
    textInput->enable(surface.get());
    textInput->showInputPanel();
    activateSpy.wait(200);
    QVERIFY(activateSpy.isEmpty());
    QVERIFY(InputMethod::self()->isActive());
    auto keyboardClient = Test::inputPanelClient();
    QVERIFY(keyboardClient);
    textInput->enable(surface.get());

    QVERIFY(InputMethod::self()->isActive());

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}

void InputMethodTest::testHidePanel()
{
    QVERIFY(!InputMethod::self()->isActive());

    QSignalSpy clientAddedSpy(workspace(), &Workspace::clientAdded);
    QSignalSpy clientRemovedSpy(workspace(), &Workspace::clientRemoved);
    QVERIFY(clientAddedSpy.isValid());

    QSignalSpy activateSpy(InputMethod::self(), &InputMethod::activeChanged);
    QScopedPointer<TextInput> textInput(Test::waylandTextInputManager()->createTextInput(Test::waylandSeat()));
    textInput->showInputPanel();
    QVERIFY(clientAddedSpy.wait());

    // Create an xdg_toplevel surface and wait for the compositor to catch up.
    QScopedPointer<Surface> surface(Test::createSurface());
    QScopedPointer<Test::XdgToplevel> shellSurface(Test::createXdgToplevelSurface(surface.data()));
    AbstractClient *client = Test::renderAndWaitForShown(surface.data(), QSize(1280, 1024), Qt::red);
    waylandServer()->seat()->setFocusedTextInputSurface(client->surface());

    QCOMPARE(workspace()->activeClient(), client);

    QCOMPARE(clientAddedSpy.count(), 2);
    QVERIFY(activateSpy.count() || activateSpy.wait());
    QVERIFY(InputMethod::self()->isActive());

    auto keyboardClient = Test::inputPanelClient();
    auto ipsurface = Test::inputPanelSurface();
    QVERIFY(keyboardClient);
    clientRemovedSpy.clear();
    delete ipsurface;
    QVERIFY(InputMethod::self()->isVisible());
    QVERIFY(clientRemovedSpy.count() || clientRemovedSpy.wait());
    QVERIFY(!InputMethod::self()->isVisible());

    // Destroy the test client.
    shellSurface.reset();
    QVERIFY(Test::waitForWindowDestroyed(client));
}

void InputMethodTest::testSwitchFocusedSurfaces()
{
    QVERIFY(!InputMethod::self()->isActive());

    QSignalSpy clientAddedSpy(workspace(), &Workspace::clientAdded);
    QSignalSpy clientRemovedSpy(workspace(), &Workspace::clientRemoved);
    QVERIFY(clientAddedSpy.isValid());

    QSignalSpy activateSpy(InputMethod::self(), &InputMethod::activeChanged);
    QScopedPointer<TextInput> textInput(Test::waylandTextInputManager()->createTextInput(Test::waylandSeat()));
    textInput->showInputPanel();
    QVERIFY(clientAddedSpy.wait(10000));

    QVector<AbstractClient *> clients;
    QVector<Surface *> surfaces;
    QVector<Test::XdgToplevel *> toplevels;
    // We create 3 surfaces
    for (int i = 0; i < 3; ++i) {
        auto surface = Test::createSurface();
        auto shellSurface = Test::createXdgToplevelSurface(surface);
        clients += Test::renderAndWaitForShown(surface, QSize(1280, 1024), Qt::red);
        QCOMPARE(workspace()->activeClient(), clients.constLast());
        surfaces += surface;
        toplevels += shellSurface;
    }
    waylandServer()->seat()->setFocusedTextInputSurface(clients.constFirst()->surface());

    QCOMPARE(clientAddedSpy.count(), 4);
    QVERIFY(activateSpy.count() || activateSpy.wait());
    QVERIFY(!InputMethod::self()->isActive());
    textInput->enable(surfaces.last());
    QVERIFY(!InputMethod::self()->isActive());
    waylandServer()->seat()->setFocusedTextInputSurface(clients.first()->surface());
    QVERIFY(!InputMethod::self()->isActive());
    activateSpy.clear();
    waylandServer()->seat()->setFocusedTextInputSurface(clients.last()->surface());
    QVERIFY(activateSpy.count() || activateSpy.wait());
    QVERIFY(InputMethod::self()->isActive());

    activateSpy.clear();
    waylandServer()->seat()->setFocusedTextInputSurface(clients.first()->surface());
    QVERIFY(activateSpy.count() || activateSpy.wait());
    QVERIFY(!InputMethod::self()->isActive());

    // Destroy the test client.
    for (int i = 0; i < clients.count(); ++i) {
        delete toplevels[i];
        QVERIFY(Test::waitForWindowDestroyed(clients[i]));
    }
}

WAYLANDTEST_MAIN(InputMethodTest)

#include "inputmethod_test.moc"
