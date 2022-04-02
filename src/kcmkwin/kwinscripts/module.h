/*
    SPDX-FileCopyrightText: 2011 Tamas Krutki <ktamasw@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef MODULE_H
#define MODULE_H

#include <KCModule>
#include <KPluginMetaData>
#include <KPluginModel>
#include <KQuickAddons/ConfigModule>
#include <KSharedConfig>
#include <kpluginmetadata.h>

class KJob;
class KWinScriptsData;

class Module : public KQuickAddons::ConfigModule
{
    Q_OBJECT
        
    Q_PROPERTY(QAbstractItemModel *effectsModel READ effectsModel CONSTANT)
    Q_PROPERTY(QList<KPluginMetaData> pendingDeletions READ pendingDeletions NOTIFY pendingDeletionsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY messageChanged)
    Q_PROPERTY(QString infoMessage READ infoMessage NOTIFY messageChanged)
public:
    explicit Module(QObject *parent, const KPluginMetaData &data, const QVariantList &args = QVariantList());

    void load() override;
    void save() override;
    void defaults() override;

    QAbstractItemModel *effectsModel() const
    {
        return m_model;
    }

    Q_INVOKABLE void togglePendingDeletion(const KPluginMetaData &data);
    Q_INVOKABLE bool canDeleteEntry(const KPluginMetaData &data)
    {
        return QFileInfo(data.fileName()).isWritable();
    }

    Q_SIGNAL void pendingDeletionsChanged();
    QList<KPluginMetaData> pendingDeletions()
    {
        return m_pendingDeletions;
    }

    Q_SIGNAL void messageChanged();
    QString errorMessage() const
    {
        return m_errorMessage;
    }
    QString infoMessage() const
    {
        return m_infoMessage;
    }
    void setErrorMessage(const QString &message)
    {
        m_infoMessage.clear();
        m_errorMessage = message;
        Q_EMIT messageChanged();
    }

    /**
     * Called when the import script button is clicked.
     */
    Q_INVOKABLE void importScript();

    Q_INVOKABLE void configure(const KPluginMetaData &data);

private:
    void importScriptInstallFinished(KJob *job);

    KSharedConfigPtr m_kwinConfig;
    KWinScriptsData *m_kwinScriptsData;
    QList<KPluginMetaData> m_pendingDeletions;
    KPluginModel *m_model;
    QString m_errorMessage;
    QString m_infoMessage;
};

#endif // MODULE_H
