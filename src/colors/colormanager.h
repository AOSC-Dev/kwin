/*
    SPDX-FileCopyrightText: 2020 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "kwinglobals.h"

#include <QObject>
#include <memory>

namespace KWin
{

class Output;
class ColorDevice;
class ColorManagerPrivate;
class ColorSpace;

/**
 * The ColorManager class is the entry point into color management facilities.
 */
class KWIN_EXPORT ColorManager : public QObject
{
    Q_OBJECT

public:
    ~ColorManager() override;

    /**
     * Returns the color device for the specified @a output, or @c null if there is no
     * any device.
     */
    ColorDevice *findDevice(Output *output) const;

    /**
     * Returns the list of all available color devices.
     */
    QVector<ColorDevice *> devices() const;

    std::shared_ptr<ColorSpace> getColorSpace(const QString &path);

Q_SIGNALS:
    /**
     * This signal is emitted when a new color device @a device has been added.
     */
    void deviceAdded(ColorDevice *device);
    /**
     * This signal is emitted when a color device has been removed. @a device indicates
     * what color device was removed.
     */
    void deviceRemoved(ColorDevice *device);

private Q_SLOTS:
    void handleOutputEnabled(Output *output);
    void handleOutputDisabled(Output *output);
    void handleSessionActiveChanged(bool active);

private:
    std::unique_ptr<ColorManagerPrivate> d;
    KWIN_SINGLETON(ColorManager)
};

} // namespace KWin
