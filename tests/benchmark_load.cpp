#include <QTest>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QElapsedTimer>

#include "model/BlockModel.h"
#include "core/Document.h"
#include "tracking/MemoryTracker.h"
#include "tracking/ScopedTimer.h"

using namespace Templnote;

class BenchmarkLoad : public QObject {
    Q_OBJECT

private:
    QString createDummyDocument(int blockCount) {
        QTemporaryFile tempFile(QDir::tempPath() + "/templnote_bench_XXXXXX.templnote");
        tempFile.setAutoRemove(false);
        if (!tempFile.open()) return QString();

        QJsonObject root;
        root["version"] = 1;
        root["title"] = "Benchmark Document";
        root["createdAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root["modifiedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

        QJsonArray blocksArray;
        for (int i = 0; i < blockCount; ++i) {
            QJsonObject block;
            block["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            block["type"] = "paragraph";
            block["text"] = QString("This is sample block number %1 with a reasonable amount of text to simulate realistic note taking.").arg(i);
            blocksArray.append(block);
        }
        root["blocks"] = blocksArray;

        QJsonDocument doc(root);
        tempFile.write(doc.toJson());
        QString path = tempFile.fileName();
        tempFile.close();
        return path;
    }

    void measurePhases(const QString &path) {
        QFile file(path);
        file.open(QIODevice::ReadOnly);

        QElapsedTimer timer;
        timer.start();
        const QByteArray bytes = file.readAll();
        const double readMs = timer.elapsed();

        timer.restart();
        QJsonParseError error{};
        const QJsonDocument json = QJsonDocument::fromJson(bytes, &error);
        const double parseMs = timer.elapsed();

        timer.restart();
        Document document;
        QString documentError;
        const bool ok = document.fromJson(json.object(), &documentError);
        const double decodeMs = timer.elapsed();

        timer.restart();
        BlockModel model;
        const bool loaded = model.loadDocument(path);
        const double totalMs = timer.elapsed();

        qInfo().noquote() << QStringLiteral("PHASES ok=%1/%2 read=%3ms json=%4ms blocks=%5ms loadTotal=%6ms blocks=%7")
                                 .arg(ok)
                                 .arg(loaded)
                                 .arg(readMs, 0, 'f', 3)
                                 .arg(parseMs, 0, 'f', 3)
                                 .arg(decodeMs, 0, 'f', 3)
                                 .arg(totalMs, 0, 'f', 3)
                                 .arg(document.count());
    }

private slots:
    void initTestCase() {
        MemoryTracker tracker;
        qInfo().noquote() << "Initial Memory:" << tracker.formattedStats();
    }

    void benchmarkLoadSmallDoc_100Blocks() {
        QString path = createDummyDocument(100);
        QVERIFY(!path.isEmpty());

        BlockModel model;
        QBENCHMARK {
            model.loadDocument(path);
        }

        QFile::remove(path);
    }

    void benchmarkLoadMediumDoc_1000Blocks() {
        QString path = createDummyDocument(1000);
        QVERIFY(!path.isEmpty());

        BlockModel model;
        QBENCHMARK {
            model.loadDocument(path);
        }

        QFile::remove(path);
    }

    void benchmarkLoadLargeDoc_5000Blocks() {
        QString path = createDummyDocument(5000);
        QVERIFY(!path.isEmpty());

        BlockModel model;
        QBENCHMARK {
            model.loadDocument(path);
        }

        QFile::remove(path);
    }

    void loadPhases10000Blocks() {
        QString path = createDummyDocument(10000);
        QVERIFY(!path.isEmpty());
        measurePhases(path);
        QFile::remove(path);
    }

    void benchmarkLoadHugeDoc_10000Blocks() {
        QString path = createDummyDocument(10000);
        QVERIFY(!path.isEmpty());

        BlockModel model;
        QBENCHMARK {
            model.loadDocument(path);
        }

        QFile::remove(path);
    }

    void benchmarkLoadHugeDoc_50000Blocks() {
        QString path = createDummyDocument(50000);
        QVERIFY(!path.isEmpty());

        BlockModel model;
        QBENCHMARK {
            model.loadDocument(path);
        }

        QFile::remove(path);
    }

    void cleanupTestCase() {
        MemoryTracker tracker;
        qInfo().noquote() << "Final Memory:" << tracker.formattedStats();
    }
};

QTEST_MAIN(BenchmarkLoad)
#include "benchmark_load.moc"
