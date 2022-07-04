/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2015 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_DRM_BACKEND_H
#define KWIN_DRM_BACKEND_H
#include "platform.h"

#include "dpmsinputeventfilter.h"
#include "placeholderinputeventfilter.h"

#include <QPointer>
#include <QSize>
#include <QVector>

#include <memory>

struct gbm_bo;

namespace KWin
{

class Udev;
class UdevMonitor;
class UdevDevice;

class DrmAbstractOutput;
class Cursor;
class DrmGpu;
class DrmVirtualOutput;
class DrmRenderBackend;

class KWIN_EXPORT DrmBackend : public Platform
{
    Q_OBJECT
    Q_INTERFACES(KWin::Platform)
    Q_PLUGIN_METADATA(IID "org.kde.kwin.Platform" FILE "drm.json")
public:
    explicit DrmBackend(QObject *parent = nullptr);
    ~DrmBackend() override;

    InputBackend *createInputBackend() override;
    QPainterBackend *createQPainterBackend() override;
    OpenGLBackend *createOpenGLBackend() override;

    std::optional<DmaBufParams> testCreateDmaBuf(const QSize &size, quint32 format, const QVector<uint64_t> &modifiers) override;
    std::shared_ptr<DmaBufTexture> createDmaBufTexture(const QSize &size, quint32 format, const uint64_t modifier) override;
    Session *session() const override;
    bool initialize() override;

    Outputs outputs() const override;
    Outputs enabledOutputs() const override;

    void enableOutput(DrmAbstractOutput *output, bool enable);

    void createDpmsFilter();
    void checkOutputsAreOn();

    QVector<CompositingType> supportedCompositors() const override;

    QString supportInformation() const override;
    Output *createVirtualOutput(const QString &name, const QSize &size, double scale) override;
    void removeVirtualOutput(Output *output) override;

    DrmGpu *primaryGpu() const;
    DrmGpu *findGpu(dev_t deviceId) const;
    DrmGpu *findGpuByFd(int fd) const;

    bool isActive() const;

    void setRenderBackend(DrmRenderBackend *backend);
    DrmRenderBackend *renderBackend() const;

    void releaseBuffers();

public Q_SLOTS:
    void turnOutputsOn();
    void sceneInitialized() override;

Q_SIGNALS:
    void activeChanged();
    void gpuRemoved(DrmGpu *gpu);
    void gpuAdded(DrmGpu *gpu);

protected:
    bool applyOutputChanges(const OutputConfiguration &config) override;

private:
    friend class DrmGpu;
    void addOutput(DrmAbstractOutput *output);
    void removeOutput(DrmAbstractOutput *output);
    void activate(bool active);
    void reactivate();
    void deactivate();
    void updateOutputs();
    bool readOutputsConfiguration(const QVector<DrmAbstractOutput *> &outputs);
    void handleUdevEvent();
    DrmGpu *addGpu(const QString &fileName);

    std::unique_ptr<Udev> m_udev;
    std::unique_ptr<UdevMonitor> m_udevMonitor;
    std::unique_ptr<Session> m_session;
    // all outputs, enabled and disabled
    QVector<DrmAbstractOutput *> m_outputs;
    // only enabled outputs
    QVector<DrmAbstractOutput *> m_enabledOutputs;
    DrmVirtualOutput *m_placeHolderOutput = nullptr;

    bool m_active = false;
    const QStringList m_explicitGpus;
    QVector<DrmGpu *> m_gpus;
    std::unique_ptr<DpmsInputEventFilter> m_dpmsFilter;
    std::unique_ptr<PlaceholderInputEventFilter> m_placeholderFilter;
    DrmRenderBackend *m_renderBackend = nullptr;

    gbm_bo *createBo(const QSize &size, quint32 format, const QVector<uint64_t> &modifiers);
};

}

#endif
