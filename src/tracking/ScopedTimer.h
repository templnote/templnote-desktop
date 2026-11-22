#pragma once

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QString>
#include <functional>

namespace Templnote {

// Scoped RAII timer for micro-benchmarking and performance tracking
class ScopedTimer {
public:
    explicit ScopedTimer(const QString &operationName,
                         std::function<void(double elapsedMs)> onFinish = nullptr);
    ~ScopedTimer();

    // A copy would run two destructors over one timer and log twice (and the
    // copy would report the original's captured duration).
    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;

    // Stops the timer early and returns elapsed milliseconds
    double elapsedMs();

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
    static double measureMs(const QString &operationName, const std::function<void()> &fn);
};

} // namespace Templnote
