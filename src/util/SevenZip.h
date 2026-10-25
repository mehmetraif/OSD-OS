#pragma once
#include <QString>
#include <QStringList>
#include <functional>

// A 7z archive's files, taken out with the LZMA SDK's decoder
// (thirdparty/lzma, built in by cmake/lzma.cmake): what RetroArch's and
// MAME's builds for Windows come as (ProgramInstaller), MAME's as a
// self-extracting one, a program with the archive appended, which is read
// from where the archive begins. It decodes what 7-Zip makes by default,
// LZMA, LZMA2 and PPMd behind the BCJ, BCJ2 and other filters, not an
// encrypted archive, nor one in a method the SDK's 7z decoder leaves out
// (Deflate, BZip2, Zstandard).
namespace sevenzip {

// Where an entry of that name goes under the folder the archive is unpacked
// into: its parts with "/" between them, whichever way the archive wrote
// them. "" for a name that would leave the folder (an absolute path, a
// drive, a part that is empty, "." or ".."), or is empty: the extractor
// skips such an entry, which isn't the archive's to place.
QString safeRelativePath(const QString &entryName);

// The entries' names as the archive gives them, a folder's with "/" after
// it; empty, with why in error where given, for a file that can't be read
// as a 7z archive.
QStringList list(const QString &archive, QString *error = nullptr);

// Every file of the archive at that path, under folder (made where it isn't
// there, as the files' own are), a file already there written over; an
// entry whose name safeRelativePath() refuses is skipped. "" once all are
// written, else why not, what was written left where it is. The SDK decodes
// a solid block whole into memory before its files are written, so an
// archive is refused, before anything is written, when a block of it is
// bigger than mostBlockBytes, and when its files together are more than
// mostTotalBytes. progress, where given, is called as each file is written,
// with the bytes written so far and the whole; false from it stops the
// extraction ("Stopped").
QString extract(const QString &archive, const QString &folder, qint64 mostTotalBytes, qint64 mostBlockBytes,
                const std::function<bool(qint64 written, qint64 total)> &progress = {});

} // namespace sevenzip
