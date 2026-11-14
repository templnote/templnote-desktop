#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "../src/storage/AppPaths.h"
#include "../src/storage/DocumentListModel.h"

using namespace Templnote;

class StorageTests : public QObject {
  Q_OBJECT

private slots:

  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("TemplnoteTest");
    QCoreApplication::setApplicationName("TemplnoteTest");
  }

  void sanitizeFileName() {
    QCOMPARE(AppPaths::sanitizeFileName("  My Notes  "), QString("My Notes"));
    QCOMPARE(AppPaths::sanitizeFileName("../escape"), QString("escape"));
    QCOMPARE(AppPaths::sanitizeFileName("a/b\\c:d"), QString("a b c d"));
    QCOMPARE(AppPaths::sanitizeFileName("..."), QString());
  }

  void uniqueDocumentPathIncrements() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString first =
        AppPaths::uniqueDocumentPath(directory.path(), "My Notes");
    QCOMPARE(QFileInfo(first).fileName(), QString("My Notes.templnote"));

    QFile file(first);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    const QString second =
        AppPaths::uniqueDocumentPath(directory.path(), "My Notes");
    QCOMPARE(QFileInfo(second).fileName(), QString("My Notes 2.templnote"));
  }

  void uniqueDirectoryPathIncrements() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    const QString first =
        AppPaths::uniqueDirectoryPath(directory.path(), "Untitled Folder");
    QCOMPARE(QFileInfo(first).fileName(), QString("Untitled Folder"));

    QVERIFY(QDir().mkpath(first));

    const QString second =
        AppPaths::uniqueDirectoryPath(directory.path(), "Untitled Folder");
    QCOMPARE(QFileInfo(second).fileName(), QString("Untitled Folder 2"));
  }

  void folderCreationAndNavigation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    QCOMPARE(model.relativeDirectory(), QString());
    QCOMPARE(model.breadcrumbs().size(), 0);

    const QString folder = model.createUntitledFolder();
    QVERIFY(QFileInfo(folder).isDir());
    QCOMPARE(QFileInfo(folder).fileName(), QString("Untitled Folder"));
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), DocumentListModel::IsFolderRole)
                 .toBool(),
             true);

    QVERIFY(model.enterFolder(folder));
    QCOMPARE(model.relativeDirectory(), QString("Untitled Folder"));
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.breadcrumbs().size(), 1);

    const QString nested = model.createUntitledFolder();
    QVERIFY(QFileInfo(nested).isDir());
    QCOMPARE(QFileInfo(nested).absolutePath(),
             QFileInfo(folder).absoluteFilePath());

    QVERIFY(model.goUp());
    QCOMPARE(model.relativeDirectory(), QString());
    QVERIFY(!model.goUp());

    model.setCurrentFilePath(directory.filePath("Other.templnote"));
    QCOMPARE(model.currentFilePath(),
             QFileInfo(directory.filePath("Other.templnote")).absoluteFilePath());
  }

  void breadcrumbsShowRelativeSegments() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    const QString first = model.createUntitledFolder();
    QVERIFY(model.enterFolder(first));
    const QString second = model.createUntitledFolder();
    QVERIFY(model.enterFolder(second));

    QCOMPARE(model.relativeDirectory(),
             QString("Untitled Folder/Untitled Folder"));

    const QVariantList crumbs = model.breadcrumbs();
    QCOMPARE(crumbs.size(), 2);
    QCOMPARE(crumbs.at(0).toMap().value("name").toString(),
             QString("Untitled Folder"));
    QCOMPARE(crumbs.at(1).toMap().value("name").toString(),
             QString("Untitled Folder"));
  }

  void folderCreationCollisionGetsSuffix() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    const QString first = model.createUntitledFolder();
    const QString second = model.createUntitledFolder();

    QVERIFY(first != second);
    QCOMPARE(QFileInfo(second).fileName(), QString("Untitled Folder 2"));
  }

  void renameFolderRejectsCollisionAndNavigates() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    const QString first = model.createUntitledFolder();

    QVERIFY(model.renameFolder(first, "Ideas"));
    const QString ideas = QDir(directory.path()).filePath("Ideas");
    QVERIFY(QFileInfo(ideas).isDir());
    QCOMPARE(model.data(model.index(0, 0), DocumentListModel::NameRole)
                 .toString(),
             QString("Ideas"));

    const QString second = model.createUntitledFolder();
    QVERIFY(model.renameFolder(second, "Ideas"));
    QVERIFY(QFileInfo(QDir(directory.path()).filePath("Ideas 2")).isDir());

    QVERIFY(model.enterFolder(ideas));
    QVERIFY(model.renameFolder(ideas, "Renamed"));
    QCOMPARE(model.directory(), QFileInfo(QDir(directory.path()).filePath("Renamed"))
                                    .absoluteFilePath());
  }

  void navigationRejectsPathsOutsideRoot() {
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());

    DocumentListModel model(root.path());
    QVERIFY(!model.enterFolder(outside.path()));
    QVERIFY(!model.enterFolder(root.filePath("missing")));
    QVERIFY(!model.renameFolder(outside.path(), "Nope"));
  }

  void documentsAreCreatedInTheCurrentFolder() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    const QString folder = model.createUntitledFolder();
    QVERIFY(model.enterFolder(folder));

    const QString documentPath = model.createUntitledPath();
    QCOMPARE(QFileInfo(documentPath).absolutePath(),
             QFileInfo(folder).absoluteFilePath());

    QFile document(documentPath);
    QVERIFY(document.open(QIODevice::WriteOnly));
    document.write("{\"format\":\"templnote\",\"version\":1,\"blocks\":[]}");
    document.close();

    model.refresh();
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), DocumentListModel::IsFolderRole)
                 .toBool(),
             false);
    QCOMPARE(model.mostRecentDocumentPath(),
             QFileInfo(documentPath).absoluteFilePath());
  }

  void mostRecentDocumentPathIgnoresFolders() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    QVERIFY(!model.createUntitledFolder().isEmpty());

    const QString documentPath = directory.filePath("Notes.templnote");
    QFile document(documentPath);
    QVERIFY(document.open(QIODevice::WriteOnly));
    document.write("{\"format\":\"templnote\",\"version\":1,\"blocks\":[]}");
    document.close();

    model.refresh();
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.mostRecentDocumentPath(),
             QFileInfo(documentPath).absoluteFilePath());
  }

  void documentListTracksFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.directory(), QDir::cleanPath(directory.path()));

    const QString path = directory.filePath("Ideas.templnote");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"format\":\"templnote\",\"version\":1,\"blocks\":[]}");
    file.close();

    model.refresh();
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.filePathAt(0), QFileInfo(path).absoluteFilePath());
    QCOMPARE(model.data(model.index(0, 0), DocumentListModel::NameRole)
                 .toString(),
             QString("Ideas"));

    model.setCurrentFilePath(path);
    QCOMPARE(model.data(model.index(0, 0), DocumentListModel::CurrentRole)
                 .toBool(),
             true);

    model.setCurrentFilePath(directory.filePath("Other.templnote"));
    QCOMPARE(model.data(model.index(0, 0), DocumentListModel::CurrentRole)
                 .toBool(),
             false);
  }

  void documentListIgnoresOtherFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QFile notes(directory.filePath("notes.txt"));
    QVERIFY(notes.open(QIODevice::WriteOnly));
    notes.close();

    QFile document(directory.filePath("Real.templnote"));
    QVERIFY(document.open(QIODevice::WriteOnly));
    document.close();

    DocumentListModel model(directory.path());
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.filePathAt(0),
             QFileInfo(directory.filePath("Real.templnote")).absoluteFilePath());
  }

  void createUntitledPathUsesModelDirectory() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    const QString path = model.createUntitledPath();

    QCOMPARE(QFileInfo(path).absolutePath(), QDir(directory.path()).absolutePath());
    QCOMPARE(QFileInfo(path).fileName(), QString("Untitled.templnote"));
  }

  void appDirectoriesAreSeparate() {
    const QString documents = AppPaths::documentsDirectory();
    const QString config = AppPaths::configDirectory();
    const QString cache = AppPaths::cacheDirectory();

    QVERIFY(documents.endsWith(QStringLiteral("/Templnote")));
    QVERIFY(!config.isEmpty());
    QVERIFY(!cache.isEmpty());
    QVERIFY(documents != config);
    QVERIFY(documents != cache);
    QVERIFY(config != cache);
    QVERIFY(AppPaths::autosaveDirectory().startsWith(cache));
  }

  void deleteEntryMovesToTrash() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    const QString folder = model.createUntitledFolder();
    QVERIFY(QFileInfo::exists(folder));
    QCOMPARE(model.count(), 1);

    QVERIFY(model.deleteEntry(folder));
    QVERIFY(!QFileInfo::exists(folder));
    QCOMPARE(model.count(), 0);
  }

  void deleteNonEmptyFolderMovesFilesToRoot() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    const QString folder = model.createUntitledFolder();
    QVERIFY(QFileInfo::exists(folder));

    // Create a file inside this folder
    const QString innerFilePath = QDir(folder).filePath("InnerNote.templnote");
    QFile file(innerFilePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("{\"blocks\":[]}");
    file.close();
    QVERIFY(QFileInfo::exists(innerFilePath));

    // Delete the non-empty folder
    QVERIFY(model.deleteEntry(folder));
    QVERIFY(!QFileInfo::exists(folder));

    // The inner file must now be in the root directory
    const QString movedFilePath = QDir(directory.path()).filePath("InnerNote.templnote");
    QVERIFY(QFileInfo::exists(movedFilePath));

    model.refresh();
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.filePathAt(0), QDir::cleanPath(movedFilePath));
  }

  void renameDocumentRoundTrip() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DocumentListModel model(directory.path());
    const QString docPath = model.createUntitledPath();
    QFile file(docPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("{\"blocks\":[]}");
    file.close();

    model.refresh();
    QCOMPARE(model.count(), 1);

    const QString newPath = model.renameDocument(docPath, "Renamed Note");
    QVERIFY(!newPath.isEmpty());
    QCOMPARE(QFileInfo(newPath).fileName(), QString("Renamed Note.templnote"));
    QVERIFY(QFileInfo::exists(newPath));
    QVERIFY(!QFileInfo::exists(docPath));
    QCOMPARE(model.filePathAt(0), newPath);
  }
};

QTEST_GUILESS_MAIN(StorageTests)

#include "tst_storage.moc"