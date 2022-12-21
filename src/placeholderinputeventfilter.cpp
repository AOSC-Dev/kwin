/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2022 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "placeholderinputeventfilter.h"

namespace KWin
{

bool PlaceholderInputEventFilter::pointerEvent(QMouseEvent *event, quint32 nativeButton)
{
    return true;
}

bool PlaceholderInputEventFilter::wheelEvent(QWheelEvent *event)
{
    return true;
}

bool PlaceholderInputEventFilter::keyEvent(KeyEvent *event)
{
    return true;
}

bool PlaceholderInputEventFilter::touchDown(qint32 id, const QPointF &pos, quint32 time)
{
    return true;
}

bool PlaceholderInputEventFilter::touchMotion(qint32 id, const QPointF &pos, quint32 time)
{
    return true;
}

bool PlaceholderInputEventFilter::touchUp(qint32 id, quint32 time)
{
    return true;
}

}
