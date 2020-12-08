/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2016, 2017 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_KEYBOARD_LAYOUT_H
#define KWIN_KEYBOARD_LAYOUT_H

#include "input_event_spy.h"
#include <QObject>
#include <QVector>

#include <KSharedConfig>
typedef uint32_t xkb_layout_index_t;

class KStatusNotifierItem;
class QAction;

namespace KWin
{
class Xkb;
class KeyboardLayoutDBusInterface;

namespace KeyboardLayoutSwitching
{
class Policy;
}

class KeyboardLayout : public QObject, public InputEventSpy
{
    Q_OBJECT
public:
    explicit KeyboardLayout(Xkb *xkb);
    ~KeyboardLayout() override;

    void setConfig(KSharedConfigPtr config) {
        m_config = config;
    }

    void init();

    void checkLayoutChange(quint32 previousLayout);
    void switchToNextLayout();
    void switchToPreviousLayout();
    void resetLayout();
    void updateNotifier();

Q_SIGNALS:
    void layoutChanged();
    void layoutsReconfigured();

private Q_SLOTS:
    void reconfigure();

private:
    void initDBusInterface();
    void notifyLayoutChange();
    void initNotifierItem();
    void switchToLayout(xkb_layout_index_t index);
    void reinitNotifierMenu();
    void loadShortcuts();
    Xkb *m_xkb;
    xkb_layout_index_t m_layout = 0;
    KStatusNotifierItem *m_notifierItem;
    KSharedConfigPtr m_config;
    QVector<QAction*> m_layoutShortcuts;
    KeyboardLayoutDBusInterface *m_dbusInterface = nullptr;
    KeyboardLayoutSwitching::Policy *m_policy = nullptr;
};

class KeyboardLayoutDBusInterface : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KeyboardLayouts")

public:
    explicit KeyboardLayoutDBusInterface(Xkb *xkb, KeyboardLayout *parent);
    ~KeyboardLayoutDBusInterface() override;

public Q_SLOTS:
    void switchToNextLayout();
    void switchToPreviousLayout();
    bool setLayout(const QString &layout);
    QString getLayout() const;
    QString getLayoutDisplayName() const;
    QString getLayoutLongName() const;
    QStringList getLayoutsList() const;

Q_SIGNALS:
    void layoutChanged(QString layout);
    void layoutListChanged();

private:
    Xkb *m_xkb;
    KeyboardLayout *m_keyboardLayout;
};

}

#endif
