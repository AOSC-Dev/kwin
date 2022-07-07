/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2021 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QPoint>
#include <QSize>

#include "kwin_export.h"
#include "output.h"

namespace KWin
{

class KWIN_EXPORT OutputChangeSet
{
public:
    bool enabled;
    QPoint pos;
    float scale;
    QSize modeSize;
    uint32_t refreshRate;
    Output::Transform transform;
    uint32_t overscan;
    Output::RgbRange rgbRange;
    RenderLoop::VrrPolicy vrrPolicy;
};

class KWIN_EXPORT OutputConfiguration
{
public:
    std::shared_ptr<OutputChangeSet> changeSet(Output *output);
    std::shared_ptr<OutputChangeSet> constChangeSet(Output *output) const;
    bool hasChangeSet(Output *output) const;

private:
    QMap<Output *, std::shared_ptr<OutputChangeSet>> m_properties;
};

}
