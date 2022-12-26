/*
    SPDX-FileCopyrightText: 2022 Nicolas Fella <nicolas.fella@gmx.de>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "plugin.h"

#include "core/inputdevice.h"
#include "input.h"
#include "input_event.h"

#include <QKeySequence>
#include <QTimer>

class InputDevice : public KWin::InputDevice
{
    QString sysName() const override;
    QString name() const override;

    bool isEnabled() const override;
    void setEnabled(bool enabled) override;

    void setLeds(KWin::LEDs leds) override;
    KWin::LEDs leds() const override;

    bool isKeyboard() const override;
    bool isAlphaNumericKeyboard() const override;
    bool isPointer() const override;
    bool isTouchpad() const override;
    bool isTouch() const override;
    bool isTabletTool() const override;
    bool isTabletPad() const override;
    bool isTabletModeSwitch() const override;
    bool isLidSwitch() const override;
};

class MouseKeysFilter : public KWin::Plugin, public KWin::InputEventFilter
{
    Q_OBJECT
public:
    explicit MouseKeysFilter();

    bool keyEvent(KWin::KeyEvent *event) override;

private:
    void handleKeyEvent(KWin::KeyEvent *event);
    void loadConfig(const KConfigGroup &group);
    void delayTriggered();
    void repeatTriggered();
    void movePointer(QPointF delta);
    double deltaFactorForStep(int step) const;

    InputDevice m_inputDevice;
    KConfigWatcher::Ptr m_configWatcher;
    QMap<int, bool> m_keyStates;
    QTimer m_delayTimer;
    QTimer m_repeatTimer;
    int m_currentKey = 0;
    int m_currentStep = 0;

    bool m_enabled = false;
    int m_stepsToMax = 0;
    int m_curve = 0;
    int m_maxSpeed = 0;
    int m_delay = 0;
    int m_interval = 0;
};
