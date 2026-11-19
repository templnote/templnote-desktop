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

    // Stops the timer early and returns elapsed milliseconds
    double elapsedMs();

private:
    QString m_operationName;
    QElapsedTimer m_timer;
    std::function<void(double elapsedMs)> m_onFinish;
    bool m_finished = false;
};

// Benchmark helper for tracking operation execution duration
class BenchmarkTracker {
public:
    static double measureMs(const QString &operationName, const std::function<void()> &fn);
};

} // namespace Templnote
