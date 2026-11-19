#include <QTest>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>

#include "model/BlockModel.h"
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

    void cleanupTestCase() {
        MemoryTracker tracker;
        qInfo().noquote() << "Final Memory:" << tracker.formattedStats();
    }
};

QTEST_MAIN(BenchmarkLoad)
#include "benchmark_load.moc"
