/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2019 Martin Flöser <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "abstract_client.h"
#include "clientbufferref.h"

namespace KWin
{

class ClientBufferInternal;

class KWIN_EXPORT InternalClient : public AbstractClient
{
    Q_OBJECT

public:
    explicit InternalClient(QWindow *window);
    ~InternalClient() override;

    bool eventFilter(QObject *watched, QEvent *event) override;

    qreal bufferScale() const override;
    QString captionNormal() const override;
    QString captionSuffix() const override;
    QSize minSize() const override;
    QSize maxSize() const override;
    QRect transparentRect() const override;
    NET::WindowType windowType(bool direct = false, int supported_types = 0) const override;
    void killWindow() override;
    bool isPopupWindow() const override;
    QByteArray windowRole() const override;
    void closeWindow() override;
    bool isCloseable() const override;
    bool isMovable() const override;
    bool isMovableAcrossScreens() const override;
    bool isResizable() const override;
    bool isPlaceable() const override;
    bool noBorder() const override;
    bool userCanSetNoBorder() const override;
    bool wantsInput() const override;
    bool isInternal() const override;
    bool isLockScreen() const override;
    bool isOutline() const override;
    bool isShown(bool shaded_is_shown) const override;
    bool isHiddenInternal() const override;
    void hideClient(bool hide) override;
    void resizeWithChecks(const QSize &size) override;
    void setFrameGeometry(const QRect &rect) override;
    AbstractClient *findModal(bool allow_itself = false) override;
    bool takeFocus() override;
    void setNoBorder(bool set) override;
    void updateDecoration(bool check_workspace_pos, bool force = false) override;
    void destroyClient() override;
    bool hasPopupGrab() const override;
    void popupDone() override;
    bool hitTest(const QPoint &point) const override;

    ClientBufferRef buffer() const;
    void present(ClientBufferInternal *buffer, const QRegion &damage);
    QWindow *internalWindow() const;

protected:
    bool acceptsFocus() const override;
    bool belongsToSameApplication(const AbstractClient *other, SameApplicationChecks checks) const override;
    void doMove(int x, int y) override;
    void doInteractiveResizeSync() override;
    void updateCaption() override;

private:
    void requestGeometry(const QRect &rect);
    void commitGeometry(const QRect &rect);
    void setCaption(const QString &caption);
    void markAsMapped();
    void syncGeometryToInternalWindow();
    void updateInternalWindowGeometry();

    ClientBufferRef m_bufferRef;
    QWindow *m_internalWindow = nullptr;
    QString m_captionNormal;
    QString m_captionSuffix;
    NET::WindowType m_windowType = NET::Normal;
    Qt::WindowFlags m_internalWindowFlags = Qt::WindowFlags();
    bool m_userNoBorder = false;

    Q_DISABLE_COPY(InternalClient)
};

}
