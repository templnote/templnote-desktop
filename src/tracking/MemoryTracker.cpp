#include "MemoryTracker.h"

#include <QDebug>
#include <QFile>
#include <QTextStream>

#include <cmath>

#if defined(Q_OS_LINUX)
#include <sys/resource.h>
#include <unistd.h>
#elif defined(Q_OS_MAC)
#include <mach/mach.h>
#include <sys/resource.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#endif

namespace {
// formattedStats() prints two decimals; compare at the same precision so
// sub-0.01 MB sampling jitter does not emit a spurious statsChanged signal.
double roundedMb(double value) {
    return std::round(value * 100.0) / 100.0;
}

QString formatStats(const Templnote::MemoryStats &stats) {
    return QStringLiteral("RSS: %1 MB | Peak: %2 MB | VSZ: %3 MB")
        .arg(stats.residentSetSizeMb, 0, 'f', 2)
        .arg(stats.peakRssMb, 0, 'f', 2)
        .arg(stats.virtualMemorySizeMb, 0, 'f', 2);
}

QString logPrefix(const QString &context) {
    return context.isEmpty() ? QStringLiteral("[MemoryTracker]")
                             : QStringLiteral("[MemoryTracker - %1]").arg(context);
}
} // namespace

namespace Templnote {

MemoryTracker::MemoryTracker(QObject *parent)
    : QObject(parent) {
    refresh();
}

MemoryStats MemoryTracker::snapshot() {
    MemoryStats stats;

#if defined(Q_OS_LINUX)
    // Read from /proc/self/statm for current resident and virtual memory
    // statm fields: total_program_size, resident_set_size, shared_pages, text, lib, data, dt
    QFile statmFile(QStringLiteral("/proc/self/statm"));
    if (statmFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&statmFile);
        unsigned long sizePages = 0;
        unsigned long residentPages = 0;
        stream >> sizePages >> residentPages;
        long pageSizeBytes = sysconf(_SC_PAGESIZE);
        if (pageSizeBytes > 0) {
            stats.virtualMemorySizeMb = static_cast<double>(sizePages * pageSizeBytes) / (1024.0 * 1024.0);
            stats.residentSetSizeMb = static_cast<double>(residentPages * pageSizeBytes) / (1024.0 * 1024.0);
        }
    }

    // Read peak RSS using getrusage
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // On Linux, ru_maxrss is in kilobytes
        stats.peakRssMb = static_cast<double>(usage.ru_maxrss) / 1024.0;
    }
#elif defined(Q_OS_MAC)
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        stats.residentSetSizeMb = static_cast<double>(info.resident_size) / (1024.0 * 1024.0);
        stats.virtualMemorySizeMb = static_cast<double>(info.virtual_size) / (1024.0 * 1024.0);
    }
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // macOS reports ru_maxrss in bytes
        stats.peakRssMb = static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
    }
#elif defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        stats.residentSetSizeMb = static_cast<double>(pmc.WorkingSetSize) / (1024.0 * 1024.0);
        stats.peakRssMb = static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
        stats.virtualMemorySizeMb = static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
    }
#endif

    return stats;
}

double MemoryTracker::rssMb() const {
    return m_stats.residentSetSizeMb;
}

double MemoryTracker::peakRssMb() const {
    return m_stats.peakRssMb;
}

double MemoryTracker::vszMb() const {
    return m_stats.virtualMemorySizeMb;
}

QString MemoryTracker::formattedStats() const {
    return formatStats(m_stats);
}

void MemoryTracker::refresh() {
    const MemoryStats updated = snapshot();

    // Honour the "emits if updated" contract: compare at the displayed
    // precision so tiny fluctuations that never change formattedStats() are
    // not reported as changes.
    const bool changed =
        roundedMb(updated.residentSetSizeMb) != roundedMb(m_stats.residentSetSizeMb) ||
        roundedMb(updated.virtualMemorySizeMb) != roundedMb(m_stats.virtualMemorySizeMb) ||
        roundedMb(updated.peakRssMb) != roundedMb(m_stats.peakRssMb);

    if (!changed)
        return;

    m_stats = updated;
    emit statsChanged();
}

void MemoryTracker::logSnapshot(const QString &context) {
    const MemoryStats fresh = snapshot();
    qInfo().noquote() << QStringLiteral("%1 %2")
                             .arg(logPrefix(context), formatStats(fresh));
}

void MemoryTracker::logCurrent(const QString &context) const {
    // logCurrent() promises *current* consumption; the cached m_stats is only
    // refreshed by refresh() and would otherwise report stale values.
    logSnapshot(context);
}

} // namespace Templnote
