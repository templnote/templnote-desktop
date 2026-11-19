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
        const double ms = static_cast<double>(m_timer.nsecsElapsed()) / 1e6;
        if (m_onFinish) {
            m_onFinish(ms);
        } else {
            qInfo().noquote() << QStringLiteral("[BENCHMARK] %1: %2 ms")
                                     .arg(m_operationName)
                                     .arg(ms, 0, 'f', 3);
        }
        return ms;
    }
    return static_cast<double>(m_timer.nsecsElapsed()) / 1e6;
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
