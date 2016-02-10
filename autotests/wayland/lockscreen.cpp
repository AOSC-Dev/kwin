/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "kwin_wayland_test.h"
#include "abstract_backend.h"
#include "abstract_client.h"
#include "cursor.h"
#include "screenedge.h"
#include "screens.h"
#include "wayland_server.h"
#include "workspace.h"
#include "shell_client.h"
#include <kwineffects.h>

#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/compositor.h>
#include <KWayland/Client/event_queue.h>
#include <KWayland/Client/keyboard.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/pointer.h>
#include <KWayland/Client/shell.h>
#include <KWayland/Client/seat.h>
#include <KWayland/Client/shm_pool.h>
#include <KWayland/Client/surface.h>

//screenlocker
#include <KScreenLocker/KsldApp>

#include <linux/input.h>

namespace KWin
{

static const QString s_socketName = QStringLiteral("wayland_test_kwin_lock_screen-0");

class LockScreenTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void testPointer();
    void testPointerButton();
    void testPointerAxis();
    void testKeyboard();
    void testScreenEdge();
    void testEffects();
    void testEffectsKeyboard();
    void testMoveWindow();
    void testPointerShortcut();
    void testAxisShortcut_data();
    void testAxisShortcut();
    void testKeyboardShortcut();

private:
    void unlock();
    AbstractClient *showWindow();
    KWayland::Client::ConnectionThread *m_connection = nullptr;
    KWayland::Client::Compositor *m_compositor = nullptr;
    KWayland::Client::Seat *m_seat = nullptr;
    KWayland::Client::ShmPool *m_shm = nullptr;
    KWayland::Client::Shell *m_shell = nullptr;
    KWayland::Client::EventQueue *m_queue = nullptr;
    QThread *m_thread = nullptr;
};

class HelperEffect : public Effect
{
    Q_OBJECT
public:
    HelperEffect() {}
    ~HelperEffect() {}

    void windowInputMouseEvent(QEvent*) override {
        emit inputEvent();
    }
    void grabbedKeyboardEvent(QKeyEvent *e) override {
        emit keyEvent(e->text());
    }

Q_SIGNALS:
    void inputEvent();
    void keyEvent(const QString&);
};

#define LOCK \
    QVERIFY(!waylandServer()->isScreenLocked()); \
    QSignalSpy lockStateChangedSpy(ScreenLocker::KSldApp::self(), &ScreenLocker::KSldApp::lockStateChanged); \
    QVERIFY(lockStateChangedSpy.isValid()); \
    ScreenLocker::KSldApp::self()->lock(ScreenLocker::EstablishLock::Immediate); \
    QCOMPARE(lockStateChangedSpy.count(), 1); \
    QVERIFY(waylandServer()->isScreenLocked());

#define UNLOCK \
    QCOMPARE(lockStateChangedSpy.count(), 1); \
    unlock(); \
    if (lockStateChangedSpy.count() < 2) { \
        QVERIFY(lockStateChangedSpy.wait()); \
    } \
    QCOMPARE(lockStateChangedSpy.count(), 2); \
    QVERIFY(!waylandServer()->isScreenLocked());

#define MOTION(target) \
    waylandServer()->backend()->pointerMotion(target, timestamp++)

#define PRESS \
    waylandServer()->backend()->pointerButtonPressed(BTN_LEFT, timestamp++)

#define RELEASE \
    waylandServer()->backend()->pointerButtonReleased(BTN_LEFT, timestamp++)

#define KEYPRESS( key ) \
    waylandServer()->backend()->keyboardKeyPressed(key, timestamp++)

#define KEYRELEASE( key ) \
    waylandServer()->backend()->keyboardKeyReleased(key, timestamp++)

void LockScreenTest::unlock()
{
    using namespace ScreenLocker;
    const auto children = KSldApp::self()->children();
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (qstrcmp((*it)->metaObject()->className(), "LogindIntegration") != 0) {
            continue;
        }
        QMetaObject::invokeMethod(*it, "requestUnlock");
        break;
    }
}

AbstractClient *LockScreenTest::showWindow()
{
    using namespace KWayland::Client;
#define VERIFY(statement) \
    if (!QTest::qVerify((statement), #statement, "", __FILE__, __LINE__))\
        return nullptr;
#define COMPARE(actual, expected) \
    if (!QTest::qCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))\
        return nullptr;
    QSignalSpy clientAddedSpy(waylandServer(), &WaylandServer::shellClientAdded);
    VERIFY(clientAddedSpy.isValid());

    Surface *surface = m_compositor->createSurface(m_compositor);
    VERIFY(surface);
    ShellSurface *shellSurface = m_shell->createSurface(surface, surface);
    VERIFY(shellSurface);
    // let's render
    QImage img(QSize(100, 50), QImage::Format_ARGB32);
    img.fill(Qt::blue);
    surface->attachBuffer(m_shm->createBuffer(img));
    surface->damage(QRect(0, 0, 100, 50));
    surface->commit(Surface::CommitFlag::None);

    m_connection->flush();
    VERIFY(clientAddedSpy.wait());
    AbstractClient *c = workspace()->activeClient();
    VERIFY(c);
    COMPARE(clientAddedSpy.first().first().value<ShellClient*>(), c);

#undef VERIFY
#undef COMPARE

    return c;
}

void LockScreenTest::initTestCase()
{
    qRegisterMetaType<KWin::ShellClient*>();
    qRegisterMetaType<KWin::AbstractClient*>();
    QSignalSpy workspaceCreatedSpy(kwinApp(), &Application::workspaceCreated);
    QVERIFY(workspaceCreatedSpy.isValid());
    waylandServer()->backend()->setInitialWindowSize(QSize(1280, 1024));
    QMetaObject::invokeMethod(waylandServer()->backend(), "setOutputCount", Qt::DirectConnection, Q_ARG(int, 2));
    waylandServer()->init(s_socketName.toLocal8Bit());

    kwinApp()->start();
    QVERIFY(workspaceCreatedSpy.wait());
    QCOMPARE(screens()->count(), 2);
    QCOMPARE(screens()->geometry(0), QRect(0, 0, 1280, 1024));
    QCOMPARE(screens()->geometry(1), QRect(1280, 0, 1280, 1024));
    setenv("QT_QPA_PLATFORM", "wayland", true);
    waylandServer()->initWorkspace();
}

void LockScreenTest::init()
{
    using namespace KWayland::Client;
    // setup connection
    m_connection = new ConnectionThread;
    QSignalSpy connectedSpy(m_connection, &ConnectionThread::connected);
    QVERIFY(connectedSpy.isValid());
    m_connection->setSocketName(s_socketName);

    m_thread = new QThread(this);
    m_connection->moveToThread(m_thread);
    m_thread->start();

    m_connection->initConnection();
    QVERIFY(connectedSpy.wait());

    m_queue = new EventQueue(this);
    QVERIFY(!m_queue->isValid());
    m_queue->setup(m_connection);
    QVERIFY(m_queue->isValid());

    Registry registry;
    registry.setEventQueue(m_queue);
    QSignalSpy compositorSpy(&registry, &Registry::compositorAnnounced);
    QSignalSpy shmSpy(&registry, &Registry::shmAnnounced);
    QSignalSpy shellSpy(&registry, &Registry::shellAnnounced);
    QSignalSpy seatSpy(&registry, &Registry::seatAnnounced);
    QSignalSpy allAnnounced(&registry, &Registry::interfacesAnnounced);
    QVERIFY(allAnnounced.isValid());
    QVERIFY(shmSpy.isValid());
    QVERIFY(shellSpy.isValid());
    QVERIFY(compositorSpy.isValid());
    QVERIFY(seatSpy.isValid());
    registry.create(m_connection->display());
    QVERIFY(registry.isValid());
    registry.setup();
    QVERIFY(allAnnounced.wait());
    QVERIFY(!compositorSpy.isEmpty());
    QVERIFY(!shmSpy.isEmpty());
    QVERIFY(!shellSpy.isEmpty());
    QVERIFY(!seatSpy.isEmpty());

    m_compositor = registry.createCompositor(compositorSpy.first().first().value<quint32>(), compositorSpy.first().last().value<quint32>(), this);
    QVERIFY(m_compositor->isValid());
    m_shm = registry.createShmPool(shmSpy.first().first().value<quint32>(), shmSpy.first().last().value<quint32>(), this);
    QVERIFY(m_shm->isValid());
    m_shell = registry.createShell(shellSpy.first().first().value<quint32>(), shellSpy.first().last().value<quint32>(), this);
    QVERIFY(m_shell->isValid());
    m_seat = registry.createSeat(seatSpy.first().first().value<quint32>(), seatSpy.first().last().value<quint32>(), this);
    QVERIFY(m_seat->isValid());
    QSignalSpy hasPointerSpy(m_seat, &Seat::hasPointerChanged);
    QVERIFY(hasPointerSpy.isValid());
    QVERIFY(hasPointerSpy.wait());

    screens()->setCurrent(0);
    Cursor::setPos(QPoint(640, 512));
}

void LockScreenTest::cleanup()
{
    delete m_compositor;
    m_compositor = nullptr;
    delete m_seat;
    m_seat = nullptr;
    delete m_shm;
    m_shm = nullptr;
    delete m_shell;
    m_shell = nullptr;
    delete m_queue;
    m_queue = nullptr;
    if (m_thread) {
        m_connection->deleteLater();
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
        m_connection = nullptr;
    }
}

void LockScreenTest::testPointer()
{
    using namespace KWayland::Client;

    QScopedPointer<Pointer> pointer(m_seat->createPointer());
    QVERIFY(!pointer.isNull());
    QSignalSpy enteredSpy(pointer.data(), &Pointer::entered);
    QVERIFY(enteredSpy.isValid());
    QSignalSpy leftSpy(pointer.data(), &Pointer::left);
    QVERIFY(leftSpy.isValid());

    AbstractClient *c = showWindow();
    QVERIFY(c);

    // first move cursor into the center of the window
    quint32 timestamp = 1;
    MOTION(c->geometry().center());
    QVERIFY(enteredSpy.wait());

    LOCK

    QEXPECT_FAIL("", "Adding the lock screen window should send left event", Continue);
    QVERIFY(leftSpy.wait(100));
    QEXPECT_FAIL("", "Adding the lock screen window should send left event", Continue);
    QCOMPARE(leftSpy.count(), 1);

    // simulate moving out in and out again
    MOTION(c->geometry().center());
    MOTION(c->geometry().bottomRight() + QPoint(100, 100));
    MOTION(c->geometry().bottomRight() + QPoint(100, 100));
    QEXPECT_FAIL("", "Adding the lock screen window should send left event", Continue);
    QVERIFY(!leftSpy.wait(100));
    QCOMPARE(leftSpy.count(), 1);
    QCOMPARE(enteredSpy.count(), 1);

    // go back on the window
    MOTION(c->geometry().center());
    // and unlock
    UNLOCK

    QEXPECT_FAIL("", "Focus doesn't move back on surface removal", Continue);
    QVERIFY(enteredSpy.wait(100));
    QEXPECT_FAIL("", "Focus doesn't move back on surface removal", Continue);
    QCOMPARE(enteredSpy.count(), 2);
    // move on the window
    MOTION(c->geometry().center() + QPoint(100, 100));
    MOTION(c->geometry().center());
    QEXPECT_FAIL("", "Focus doesn't move back on surface removal", Continue);
    QVERIFY(!enteredSpy.wait(100));
    QCOMPARE(enteredSpy.count(), 2);
}

void LockScreenTest::testPointerButton()
{
    using namespace KWayland::Client;

    QScopedPointer<Pointer> pointer(m_seat->createPointer());
    QVERIFY(!pointer.isNull());
    QSignalSpy buttonChangedSpy(pointer.data(), &Pointer::buttonStateChanged);
    QVERIFY(buttonChangedSpy.isValid());

    AbstractClient *c = showWindow();
    QVERIFY(c);

    // first move cursor into the center of the window
    quint32 timestamp = 1;
    MOTION(c->geometry().center());
    // and simulate a click
    PRESS;
    QVERIFY(buttonChangedSpy.wait());
    RELEASE;
    QVERIFY(buttonChangedSpy.wait());

    LOCK

    // and simulate a click
    PRESS;
    QVERIFY(!buttonChangedSpy.wait(100));
    RELEASE;
    QVERIFY(!buttonChangedSpy.wait(100));

    UNLOCK

    // and click again
    PRESS;
    QVERIFY(buttonChangedSpy.wait());
    RELEASE;
    QVERIFY(buttonChangedSpy.wait());
}

void LockScreenTest::testPointerAxis()
{
    using namespace KWayland::Client;

    QScopedPointer<Pointer> pointer(m_seat->createPointer());
    QVERIFY(!pointer.isNull());
    QSignalSpy axisChangedSpy(pointer.data(), &Pointer::axisChanged);
    QVERIFY(axisChangedSpy.isValid());

    AbstractClient *c = showWindow();
    QVERIFY(c);

    // first move cursor into the center of the window
    quint32 timestamp = 1;
    MOTION(c->geometry().center());
    // and simulate axis
    waylandServer()->backend()->pointerAxisHorizontal(5.0, timestamp++);
    QVERIFY(axisChangedSpy.wait());

    LOCK

    // and simulate axis
    waylandServer()->backend()->pointerAxisHorizontal(5.0, timestamp++);
    QVERIFY(!axisChangedSpy.wait(100));
    waylandServer()->backend()->pointerAxisVertical(5.0, timestamp++);
    QVERIFY(!axisChangedSpy.wait(100));

    // and unlock
    UNLOCK

    // and move axis again
    waylandServer()->backend()->pointerAxisHorizontal(5.0, timestamp++);
    QVERIFY(axisChangedSpy.wait());
    waylandServer()->backend()->pointerAxisVertical(5.0, timestamp++);
    QVERIFY(axisChangedSpy.wait());
}

void LockScreenTest::testKeyboard()
{
    using namespace KWayland::Client;

    QScopedPointer<Keyboard> keyboard(m_seat->createKeyboard());
    QVERIFY(!keyboard.isNull());
    QSignalSpy enteredSpy(keyboard.data(), &Keyboard::entered);
    QVERIFY(enteredSpy.isValid());
    QSignalSpy leftSpy(keyboard.data(), &Keyboard::left);
    QVERIFY(leftSpy.isValid());
    QSignalSpy keyChangedSpy(keyboard.data(), &Keyboard::keyChanged);
    QVERIFY(keyChangedSpy.isValid());

    AbstractClient *c = showWindow();
    QVERIFY(c);
    QVERIFY(enteredSpy.wait());
    QTRY_COMPARE(enteredSpy.count(), 1);

    quint32 timestamp = 1;
    KEYPRESS(KEY_A);
    QVERIFY(keyChangedSpy.wait());
    QCOMPARE(keyChangedSpy.count(), 1);
    QCOMPARE(keyChangedSpy.at(0).at(0).value<quint32>(), quint32(KEY_A));
    QCOMPARE(keyChangedSpy.at(0).at(1).value<Keyboard::KeyState>(), Keyboard::KeyState::Pressed);
    QCOMPARE(keyChangedSpy.at(0).at(2).value<quint32>(), quint32(1));
    KEYRELEASE(KEY_A);
    QVERIFY(keyChangedSpy.wait());
    QCOMPARE(keyChangedSpy.count(), 2);
    QCOMPARE(keyChangedSpy.at(1).at(0).value<quint32>(), quint32(KEY_A));
    QCOMPARE(keyChangedSpy.at(1).at(1).value<Keyboard::KeyState>(), Keyboard::KeyState::Released);
    QCOMPARE(keyChangedSpy.at(1).at(2).value<quint32>(), quint32(2));

    LOCK
    KEYPRESS(KEY_B);
    KEYRELEASE(KEY_B);
    QVERIFY(leftSpy.wait());
    QCOMPARE(leftSpy.count(), 1);
    QCOMPARE(keyChangedSpy.count(), 2);

    UNLOCK
    KEYPRESS(KEY_C);
    QVERIFY(keyChangedSpy.wait());
    QCOMPARE(keyChangedSpy.count(), 3);
    KEYRELEASE(KEY_C);
    QVERIFY(keyChangedSpy.wait());
    QCOMPARE(keyChangedSpy.count(), 4);
    QCOMPARE(enteredSpy.count(), 2);
    QCOMPARE(keyChangedSpy.at(2).at(0).value<quint32>(), quint32(KEY_C));
    QCOMPARE(keyChangedSpy.at(3).at(0).value<quint32>(), quint32(KEY_C));
    QCOMPARE(keyChangedSpy.at(2).at(2).value<quint32>(), quint32(5));
    QCOMPARE(keyChangedSpy.at(3).at(2).value<quint32>(), quint32(6));
    QCOMPARE(keyChangedSpy.at(2).at(1).value<Keyboard::KeyState>(), Keyboard::KeyState::Pressed);
    QCOMPARE(keyChangedSpy.at(3).at(1).value<Keyboard::KeyState>(), Keyboard::KeyState::Released);
}

void LockScreenTest::testScreenEdge()
{
    QSignalSpy screenEdgeSpy(ScreenEdges::self(), &ScreenEdges::approaching);
    QVERIFY(screenEdgeSpy.isValid());
    QCOMPARE(screenEdgeSpy.count(), 0);

    quint32 timestamp = 1;
    MOTION(QPoint(5, 5));
    QCOMPARE(screenEdgeSpy.count(), 1);

    LOCK

    MOTION(QPoint(4, 4));
    QCOMPARE(screenEdgeSpy.count(), 1);

    // and unlock
    UNLOCK

    MOTION(QPoint(5, 5));
    QCOMPARE(screenEdgeSpy.count(), 2);
}

void LockScreenTest::testEffects()
{
    QScopedPointer<HelperEffect> effect(new HelperEffect);
    QSignalSpy inputSpy(effect.data(), &HelperEffect::inputEvent);
    QVERIFY(inputSpy.isValid());
    effects->startMouseInterception(effect.data(), Qt::ArrowCursor);

    quint32 timestamp = 1;
    QCOMPARE(inputSpy.count(), 0);
    MOTION(QPoint(5, 5));
    QCOMPARE(inputSpy.count(), 1);
    // simlate click
    PRESS;
    QCOMPARE(inputSpy.count(), 2);
    RELEASE;
    QCOMPARE(inputSpy.count(), 3);

    LOCK

    MOTION(QPoint(6, 6));
    QCOMPARE(inputSpy.count(), 3);
    // simlate click
    PRESS;
    QCOMPARE(inputSpy.count(), 3);
    RELEASE;
    QCOMPARE(inputSpy.count(), 3);

    UNLOCK

    MOTION(QPoint(5, 5));
    QCOMPARE(inputSpy.count(), 4);
    // simlate click
    PRESS;
    QCOMPARE(inputSpy.count(), 5);
    RELEASE;
    QCOMPARE(inputSpy.count(), 6);

    effects->stopMouseInterception(effect.data());
}

void LockScreenTest::testEffectsKeyboard()
{
    QScopedPointer<HelperEffect> effect(new HelperEffect);
    QSignalSpy inputSpy(effect.data(), &HelperEffect::keyEvent);
    QVERIFY(inputSpy.isValid());
    effects->grabKeyboard(effect.data());

    quint32 timestamp = 1;
    KEYPRESS(KEY_A);
    QCOMPARE(inputSpy.count(), 1);
    QCOMPARE(inputSpy.first().first().toString(), QStringLiteral("a"));
    KEYRELEASE(KEY_A);
    QCOMPARE(inputSpy.count(), 2);
    QCOMPARE(inputSpy.first().first().toString(), QStringLiteral("a"));
    QCOMPARE(inputSpy.at(1).first().toString(), QStringLiteral("a"));

    LOCK
    KEYPRESS(KEY_B);
    QCOMPARE(inputSpy.count(), 2);
    KEYRELEASE(KEY_B);
    QCOMPARE(inputSpy.count(), 2);

    UNLOCK
    KEYPRESS(KEY_C);
    QCOMPARE(inputSpy.count(), 3);
    QCOMPARE(inputSpy.first().first().toString(), QStringLiteral("a"));
    QCOMPARE(inputSpy.at(1).first().toString(), QStringLiteral("a"));
    QCOMPARE(inputSpy.at(2).first().toString(), QStringLiteral("c"));
    KEYRELEASE(KEY_C);
    QCOMPARE(inputSpy.count(), 4);
    QCOMPARE(inputSpy.first().first().toString(), QStringLiteral("a"));
    QCOMPARE(inputSpy.at(1).first().toString(), QStringLiteral("a"));
    QCOMPARE(inputSpy.at(2).first().toString(), QStringLiteral("c"));
    QCOMPARE(inputSpy.at(3).first().toString(), QStringLiteral("c"));

    effects->ungrabKeyboard();
}

void LockScreenTest::testMoveWindow()
{
    using namespace KWayland::Client;
    AbstractClient *c = showWindow();
    QVERIFY(c);
    QSignalSpy clientStepUserMovedResizedSpy(c, &AbstractClient::clientStepUserMovedResized);
    QVERIFY(clientStepUserMovedResizedSpy.isValid());
    quint32 timestamp = 1;

    workspace()->slotWindowMove();
    QCOMPARE(workspace()->getMovingClient(), c);
    QVERIFY(c->isMove());
    waylandServer()->backend()->keyboardKeyPressed(KEY_RIGHT, timestamp++);
    waylandServer()->backend()->keyboardKeyReleased(KEY_RIGHT, timestamp++);
    QEXPECT_FAIL("", "First event is ignored", Continue);
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

    // TODO adjust once the expected fail is fixed
    waylandServer()->backend()->keyboardKeyPressed(KEY_RIGHT, timestamp++);
    waylandServer()->backend()->keyboardKeyReleased(KEY_RIGHT, timestamp++);
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

    // while locking our window should continue to be in move resize
    LOCK
    QCOMPARE(workspace()->getMovingClient(), c);
    QVERIFY(c->isMove());
    waylandServer()->backend()->keyboardKeyPressed(KEY_RIGHT, timestamp++);
    waylandServer()->backend()->keyboardKeyReleased(KEY_RIGHT, timestamp++);
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 1);

    UNLOCK
    QCOMPARE(workspace()->getMovingClient(), c);
    QVERIFY(c->isMove());
    waylandServer()->backend()->keyboardKeyPressed(KEY_RIGHT, timestamp++);
    waylandServer()->backend()->keyboardKeyReleased(KEY_RIGHT, timestamp++);
    QCOMPARE(clientStepUserMovedResizedSpy.count(), 2);
    waylandServer()->backend()->keyboardKeyPressed(KEY_ESC, timestamp++);
    waylandServer()->backend()->keyboardKeyReleased(KEY_ESC, timestamp++);
    QVERIFY(!c->isMove());
}

void LockScreenTest::testPointerShortcut()
{
    using namespace KWayland::Client;
    QScopedPointer<QAction> action(new QAction(nullptr));
    QSignalSpy actionSpy(action.data(), &QAction::triggered);
    QVERIFY(actionSpy.isValid());
    input()->registerPointerShortcut(Qt::MetaModifier, Qt::LeftButton, action.data());

    // try to trigger the shortcut
    quint32 timestamp = 1;
#define PERFORM(expectedCount) \
    waylandServer()->backend()->keyboardKeyPressed(KEY_LEFTMETA, timestamp++); \
    PRESS; \
    QCoreApplication::instance()->processEvents(); \
    QCOMPARE(actionSpy.count(), expectedCount); \
    RELEASE; \
    waylandServer()->backend()->keyboardKeyReleased(KEY_LEFTMETA, timestamp++); \
    QCoreApplication::instance()->processEvents(); \
    QCOMPARE(actionSpy.count(), expectedCount);

    PERFORM(1)

    // now the same thing with a locked screen
    LOCK
    PERFORM(1)

    // and as unlocked
    UNLOCK
    PERFORM(2)
#undef PERFORM
}


void LockScreenTest::testAxisShortcut_data()
{
    QTest::addColumn<Qt::Orientation>("direction");
    QTest::addColumn<int>("sign");

    QTest::newRow("up") << Qt::Vertical << 1;
    QTest::newRow("down") << Qt::Vertical << -1;
    QTest::newRow("left") << Qt::Horizontal << 1;
    QTest::newRow("right") << Qt::Horizontal << -1;
}

void LockScreenTest::testAxisShortcut()
{
    using namespace KWayland::Client;
    QScopedPointer<QAction> action(new QAction(nullptr));
    QSignalSpy actionSpy(action.data(), &QAction::triggered);
    QVERIFY(actionSpy.isValid());
    QFETCH(Qt::Orientation, direction);
    QFETCH(int, sign);
    PointerAxisDirection axisDirection = PointerAxisUp;
    if (direction == Qt::Vertical) {
        axisDirection = sign > 0 ? PointerAxisUp : PointerAxisDown;
    } else {
        axisDirection = sign > 0 ? PointerAxisLeft : PointerAxisRight;
    }
    input()->registerAxisShortcut(Qt::MetaModifier, axisDirection, action.data());

    // try to trigger the shortcut
    quint32 timestamp = 1;
#define PERFORM(expectedCount) \
    waylandServer()->backend()->keyboardKeyPressed(KEY_LEFTMETA, timestamp++); \
    if (direction == Qt::Vertical) \
        waylandServer()->backend()->pointerAxisVertical(sign * 5.0, timestamp++); \
    else \
        waylandServer()->backend()->pointerAxisHorizontal(sign * 5.0, timestamp++); \
    QCoreApplication::instance()->processEvents(); \
    QCOMPARE(actionSpy.count(), expectedCount); \
    waylandServer()->backend()->keyboardKeyReleased(KEY_LEFTMETA, timestamp++); \
    QCoreApplication::instance()->processEvents(); \
    QCOMPARE(actionSpy.count(), expectedCount);

    PERFORM(1)

    // now the same thing with a locked screen
    LOCK
    PERFORM(1)

    // and as unlocked
    UNLOCK
    PERFORM(2)
#undef PERFORM
}

void LockScreenTest::testKeyboardShortcut()
{
    using namespace KWayland::Client;
    QScopedPointer<QAction> action(new QAction(nullptr));
    QSignalSpy actionSpy(action.data(), &QAction::triggered);
    QVERIFY(actionSpy.isValid());
    input()->registerShortcut(Qt::CTRL + Qt::META + Qt::ALT + Qt::Key_Space, action.data());

    // try to trigger the shortcut
    quint32 timestamp = 1;
    KEYPRESS(KEY_LEFTCTRL);
    KEYPRESS(KEY_LEFTMETA);
    KEYPRESS(KEY_LEFTALT);
    KEYPRESS(KEY_SPACE);
    QCoreApplication::instance()->processEvents();
    QCOMPARE(actionSpy.count(), 1);
    KEYRELEASE(KEY_SPACE);
    QCoreApplication::instance()->processEvents();
    QCOMPARE(actionSpy.count(), 1);

    LOCK
    KEYPRESS(KEY_SPACE);
    QCoreApplication::instance()->processEvents();
    QCOMPARE(actionSpy.count(), 1);
    KEYRELEASE(KEY_SPACE);
    QCoreApplication::instance()->processEvents();
    QCOMPARE(actionSpy.count(), 1);

    UNLOCK
    KEYPRESS(KEY_SPACE);
    QCoreApplication::instance()->processEvents();
    QCOMPARE(actionSpy.count(), 2);
    KEYRELEASE(KEY_SPACE);
    QCoreApplication::instance()->processEvents();
    QCOMPARE(actionSpy.count(), 2);
    KEYRELEASE(KEY_LEFTCTRL);
    KEYRELEASE(KEY_LEFTMETA);
    KEYRELEASE(KEY_LEFTALT);
}

}

WAYLANTEST_MAIN(KWin::LockScreenTest)
#include "lockscreen.moc"
