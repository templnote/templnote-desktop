#pragma once

#include <QString>

namespace Templnote {
// Single source of truth for the three storage locations. Keeping documents,
// app settings and autosave/cache data apart is a hard requirement, so nothing
// outside this file should call QStandardPaths directly.
namespace AppPaths {
// Directory the user sees in their file manager. Created on demand.
QString documentsDirectory();

// Same as documentsDirectory(), but creates it first. Returns an empty string
// and sets *error when the directory cannot be created.
QString ensureDocumentsDirectory(QString *error = nullptr);

// App settings (QSettings, window state, preferences).
QString configDirectory();

// Autosave and cache data. Deliberately separate from the user documents.
QString cacheDirectory();
QString autosaveDirectory();

// Turns arbitrary text into a safe single file name (no separators, no
// leading dots, no reserved characters).
QString sanitizeFileName(const QString &name);

// Picks a free path inside `directory` using `My Notes.templnote`,
// `My Notes 2.templnote`, ... The directory is not created here.
QString uniqueDocumentPath(const QString &directory, const QString &baseName);

// Convenience overload that uses (and ensures) the user document directory.
QString uniqueDocumentPath(const QString &baseName = QStringLiteral("Untitled"));

// Picks a free directory inside `parentDirectory` using `Untitled Folder`,
// `Untitled Folder 2`, ... The chosen directory is not created here.
QString uniqueDirectoryPath(const QString &parentDirectory,
                            const QString &baseName);
} // namespace AppPaths
} // namespace Templnote