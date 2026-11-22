#include "ScopedTimer.h"

#include <QDebug>

namespace Templnote {

ScopedTimer::ScopedTimer(const QString &operationName,
                         std::function<void(double elapsedMs)> onFinish)
    : m_operationName(operationName), m_onFinish(std::move(onFinish)) {
    m_timer.start();
}

ScopedTimer::~ScopedTimer() {
    if (!m_finished) {
        elapsedMs();
    }
}

double ScopedTimer::elapsedMs() {
    if (!m_finished) {
        m_finished = true;
        m_elapsedMs = static_cast<double>(m_timer.nsecsElapsed()) / 1e6;
        if (m_onFinish) {
            m_onFinish(m_elapsedMs);
        } else {
            qInfo().noquote() << QStringLiteral("[BENCHMARK] %1: %2 ms")
                                     .arg(m_operationName)
                                     .arg(m_elapsedMs, 0, 'f', 3);
        }
    }
    // Always report the duration recorded on completion, not the time that
    // has passed since construction on later calls.
    return m_elapsedMs;
}

double BenchmarkTracker::measureMs(const QString &operationName, const std::function<void()> &fn) {
    QElapsedTimer timer;
    timer.start();
    fn();
    const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    qInfo().noquote() << QStringLiteral("[BENCHMARK] %1: %2 ms")
                             .arg(operationName)
                             .arg(ms, 0, 'f', 3);
    return ms;
}

} // namespace Templnote
