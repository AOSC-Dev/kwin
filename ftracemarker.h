#pragma once

#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QScopeGuard>
#include <QTextStream>

#include <kwinglobals.h>

namespace KWin
{
/**
 * FTraceLogger is a singleton utility for writing log messages using ftrace
 *
 * Usage: Either:
 *  Set the KWIN_PERF_FTRACE environment variable before starting the application
 *  Calling on DBus /FTrace org.kde.kwin.FTrace.setEnabled true
 * After having created the ftrace mount
 */
class KWIN_EXPORT FTraceLogger : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.kwin.FTrace");
    Q_PROPERTY(bool isEnabled READ isEnabled NOTIFY enabledChanged)
public:
    ~FTraceLogger() = default;

    /**
     * Enabled through DBus and logging has started
     */
    bool isEnabled();

    /**
     * Main log function
     * Takes any number of arguments that can be written into QTextStream
     */
    template<typename... Args> void trace(Args... args)
    {
        Q_ASSERT(isEnabled());
        QMutexLocker lock(&m_mutex);
        if (!m_file.isOpen()) {
            return;
        }
        QTextStream stream(&m_file);
        (stream << ... << args) << Qt::endl;
    }

Q_SIGNALS:
    void enabledChanged();

public Q_SLOTS:
    Q_SCRIPTABLE void setEnabled(bool enabled);

private:
    static QString filePath();
    bool open();
    QFile m_file;
    QMutex m_mutex;
    KWIN_SINGLETON(FTraceLogger)
};

class KWIN_EXPORT FTraceDuration
{
public:
    template<typename... Args> FTraceDuration(Args... args)
    {
        static qulonglong s_context = 0;
        QTextStream stream(&m_message);
        (stream << ... << args);
        stream.flush();
        FTraceLogger::self()->trace(m_message, " begin_ctx=", ++s_context);
        m_ctx = s_context;
    }

    ~FTraceDuration();

private:
    QByteArray m_message;
    qulonglong m_ctx;
};

} // namespace KWin

/**
 * Optimised macro, arguments are only copied if tracing is enabled
 */
#define fTrace(...)                                                                                                                                                                                                                            \
    if (KWin::FTraceLogger::self()->isEnabled())                                                                                                                                                                                               \
        KWin::FTraceLogger::self()->trace(__VA_ARGS__);

/**
 * Will insert two markers into the log. Once when called, and the second at the end of the relevant block
 * In GPUVis this will appear as a timed block
 */
#define fTraceDuration(...) QScopedPointer<KWin::FTraceDuration> _duration(KWin::FTraceLogger::self()->isEnabled() ? nullptr : new KWin::FTraceDuration(__VA_ARGS__));
