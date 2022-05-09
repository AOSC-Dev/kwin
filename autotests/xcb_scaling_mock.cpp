/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Aleix Pol Gonzalez <aleixpol@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <utils/xcbutils.h>

namespace KWin
{

uint32_t Xcb::toXNative(uint value)
{
    return value;
}

uint32_t Xcb::fromXNative(uint value)
{
    return value;
}

QSize Xcb::fromXNative(const QSize &value)
{
    return value;
}

}
