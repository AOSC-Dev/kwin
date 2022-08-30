/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "output.h"
#include "wayland/output_interface.h"
#include "wayland/utils.h"
#include "wayland/xdgoutput_v1_interface.h"

#include <QTimer>

namespace KWin
{

class WaylandOutput : public QObject
{
    Q_OBJECT

public:
    explicit WaylandOutput(Output *output, QObject *parent = nullptr);

private Q_SLOTS:
    void update();
    void scheduleUpdate();

private:
    Output *m_platformOutput;
    QTimer m_updateTimer;
    std::unique_ptr<KWaylandServer::OutputInterface> m_waylandOutput;
    std::unique_ptr<KWaylandServer::XdgOutputV1Interface> m_xdgOutputV1;
};

} // namespace KWin
