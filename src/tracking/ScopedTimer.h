#pragma once

// Header-only so QtCore/Network-only targets (e.g. templnote_sync) can time
// hot paths without pulling in templnote_core just for these symbols.

#include <QDebug>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QString>
#include <functional>
#include <utility>

namespace Templnote {

Q_DECLARE_LOGGING_CATEGORY(templnoteBenchmark)

// Scoped RAII timer for micro-benchmarking and performance tracking
class ScopedTimer {
public:
    explicit ScopedTimer(const QString &operationName,
                         std::function<void(double elapsedMs)> onFinish = nullptr)
        : m_operationName(operationName), m_onFinish(std::move(onFinish)) {
        m_timer.start();
    }
    ~ScopedTimer() {
        if (!m_finished) {
            elapsedMs();
        }
    }

    // A copy would run two destructors over one timer and log twice (and the
    // copy would report the original's captured duration).
    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;

    // Stops the timer early and returns elapsed milliseconds
    double elapsedMs() {
        if (!m_finished) {
            m_finished = true;
            m_elapsedMs = static_cast<double>(m_timer.nsecsElapsed()) / 1e6;
            if (m_onFinish) {
                m_onFinish(m_elapsedMs);
            } else {
                qCDebug(templnoteBenchmark).noquote()
                    << QStringLiteral("[BENCHMARK] %1: %2 ms")
                           .arg(m_operationName)
                           .arg(m_elapsedMs, 0, 'f', 3);
            }
        }
        // Always report the duration recorded on completion, not the time that
        // has passed since construction on later calls.
        return m_elapsedMs;
    }

private:
    QString m_operationName;
    QElapsedTimer m_timer;
    std::function<void(double elapsedMs)> m_onFinish;
    bool m_finished = false;
    // Duration captured on completion; returned by every elapsedMs() call.
    double m_elapsedMs = 0.0;
};

// Benchmark helper for tracking operation execution duration
class BenchmarkTracker {
public:
    static double measureMs(const QString &operationName, const std::function<void()> &fn) {
        QElapsedTimer timer;
        timer.start();
        fn();
        const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        qCDebug(templnoteBenchmark).noquote()
            << QStringLiteral("[BENCHMARK] %1: %2 ms")
                   .arg(operationName)
                   .arg(ms, 0, 'f', 3);
        return ms;
    }
};

} // namespace Templnote
