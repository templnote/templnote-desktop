#pragma once

#include <QObject>
#include <QString>

namespace Templnote {

struct MemoryStats {
    // In Megabytes (MB)
    double residentSetSizeMb = 0.0;     // Physical memory currently used (RSS)
    double virtualMemorySizeMb = 0.0;   // Virtual memory allocated (VSZ)
    double peakRssMb = 0.0;             // Peak physical memory used (ru_maxrss)
};

class MemoryTracker : public QObject {
    Q_OBJECT
    Q_PROPERTY(double rssMb READ rssMb NOTIFY statsChanged)
    Q_PROPERTY(double peakRssMb READ peakRssMb NOTIFY statsChanged)
    Q_PROPERTY(double vszMb READ vszMb NOTIFY statsChanged)
    Q_PROPERTY(QString formattedStats READ formattedStats NOTIFY statsChanged)

public:
    explicit MemoryTracker(QObject *parent = nullptr);

    // Get current snapshot of process memory
    static MemoryStats snapshot();

    double rssMb() const;
    double peakRssMb() const;
    double vszMb() const;
    QString formattedStats() const;

    // Refresh memory stats (emits statsChanged if updated)
    Q_INVOKABLE void refresh();

    // Log current memory consumption to qDebug
    Q_INVOKABLE void logCurrent(const QString &context = QString()) const;

signals:
    void statsChanged();

private:
    MemoryStats m_stats;
};

} // namespace Templnote
