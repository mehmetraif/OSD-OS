#include "SevenZip.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QVarLengthArray>

// The LZMA SDK's 7z decoder (thirdparty/lzma): C, with its own guards.
#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include "7zFile.h"

namespace sevenzip {

namespace {

// The SDK reads the archive through a buffer of this size, as its own 7zdec
// does.
constexpr size_t kInputBufSize = size_t(1) << 18;

// What a result of the SDK's says went wrong, for the user.
QString describe(SRes res) {
    switch (res) {
    case SZ_ERROR_NO_ARCHIVE:
        return QStringLiteral("The file is not a 7z archive");
    case SZ_ERROR_ARCHIVE:
    case SZ_ERROR_DATA:
    case SZ_ERROR_CRC:
    case SZ_ERROR_INPUT_EOF:
        return QStringLiteral("The 7z archive is damaged");
    case SZ_ERROR_UNSUPPORTED:
        return QStringLiteral("The 7z archive is packed in a way this build can't unpack (encrypted, or not with LZMA)");
    case SZ_ERROR_MEM:
        return QStringLiteral("There isn't the memory to unpack the 7z archive");
    case SZ_ERROR_READ:
        return QStringLiteral("The 7z archive could not be read");
    default:
        return QStringLiteral("The 7z archive could not be unpacked (error %1)").arg(int(res));
    }
}

// An archive open for reading, with the SDK's state around it, let go of as
// it goes out of scope.
struct Archive {
    CFileInStream stream{};
    CLookToRead2 look{};
    CSzArEx db{};
    ISzAlloc alloc = { SzAlloc, SzFree };
    ISzAlloc allocTemp = { SzAlloc, SzFree };
    bool fileOpen = false;
    bool dbInit = false;

    // Opens it and reads its table of files: "" once read, else why not.
    QString open(const QString &path) {
        // The CRC tables, once for the process.
        static const bool tables = [] {
            CrcGenerateTable();
            return true;
        }();
        Q_UNUSED(tables)
#ifdef Q_OS_WIN
        const WRes opened = InFile_OpenW(&stream.file, reinterpret_cast<const wchar_t *>(path.utf16()));
#else
        const WRes opened = InFile_Open(&stream.file, QFile::encodeName(path).constData());
#endif
        if (opened != 0)
            return QStringLiteral("%1 could not be opened").arg(path);
        fileOpen = true;
        FileInStream_CreateVTable(&stream);
        stream.wres = 0;
        LookToRead2_CreateVTable(&look, False);
        look.buf = static_cast<Byte *>(ISzAlloc_Alloc(&alloc, kInputBufSize));
        if (!look.buf)
            return describe(SZ_ERROR_MEM);
        look.bufSize = kInputBufSize;
        look.realStream = &stream.vt;
        LookToRead2_INIT(&look)
        SzArEx_Init(&db);
        dbInit = true;
        const SRes res = SzArEx_Open(&db, &look.vt, &alloc, &allocTemp);
        return res == SZ_OK ? QString() : describe(res);
    }

    ~Archive() {
        if (dbInit)
            SzArEx_Free(&db, &alloc);
        if (look.buf)
            ISzAlloc_Free(&alloc, look.buf);
        if (fileOpen)
            File_Close(&stream.file);
    }

    // An entry's name as the archive has it.
    QString nameOf(size_t index) const {
        // The length counts the 0 after the name.
        const qsizetype length = qsizetype(SzArEx_GetFileNameUtf16(&db, index, nullptr));
        QVarLengthArray<UInt16, 256> name(length);
        SzArEx_GetFileNameUtf16(&db, index, name.data());
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(name.constData()), length - 1);
    }
};

} // namespace

QString safeRelativePath(const QString &entryName) {
    QString name = entryName;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // A folder's name may end in "/": the parts before it place it.
    if (name.endsWith(QLatin1Char('/')))
        name.chop(1);
    if (name.isEmpty() || name.startsWith(QLatin1Char('/')))
        return QString();
    const QStringList parts = name.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..") || part.contains(QLatin1Char(':'))
            || part.contains(QChar(0)))
            return QString();
    }
    return parts.join(QLatin1Char('/'));
}

QStringList list(const QString &archivePath, QString *error) {
    Archive archive;
    const QString problem = archive.open(archivePath);
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return {};
    }
    if (error)
        error->clear();
    QStringList names;
    names.reserve(int(archive.db.NumFiles));
    for (UInt32 i = 0; i < archive.db.NumFiles; ++i) {
        QString name = archive.nameOf(i);
        if (SzArEx_IsDir(&archive.db, i))
            name += QLatin1Char('/');
        names.append(name);
    }
    return names;
}

QString extract(const QString &archivePath, const QString &folder, qint64 mostTotalBytes, qint64 mostBlockBytes,
                const std::function<bool(qint64, qint64)> &progress) {
    Archive archive;
    const QString problem = archive.open(archivePath);
    if (!problem.isEmpty())
        return problem;
    const CSzArEx &db = archive.db;

    // Its size, and its blocks', before anything is written.
    qint64 total = 0;
    for (UInt32 i = 0; i < db.NumFiles; ++i) {
        if (!SzArEx_IsDir(&db, i))
            total += qint64(SzArEx_GetFileSize(&db, i));
    }
    if (total > mostTotalBytes) {
        return QStringLiteral("The 7z archive's files come to %1 MB, more than the %2 MB allowed")
            .arg(total >> 20).arg(mostTotalBytes >> 20);
    }
    for (UInt32 f = 0; f < db.db.NumFolders; ++f) {
        const UInt64 block = SzAr_GetFolderUnpackSize(&db.db, f);
        if (block > UInt64(mostBlockBytes))
            return QStringLiteral("A block of the 7z archive is %1 MB, too big to unpack in memory").arg(block >> 20);
    }
    if (!QDir().mkpath(folder))
        return QStringLiteral("%1 could not be made").arg(folder);

    // The block last decoded, kept for the next file of it.
    UInt32 blockIndex = 0xFFFFFFFF;
    Byte *outBuffer = nullptr;
    size_t outBufferSize = 0;
    const auto freeBlock = qScopeGuard([&] { ISzAlloc_Free(&archive.alloc, outBuffer); });
    qint64 written = 0;
    for (UInt32 i = 0; i < db.NumFiles; ++i) {
        const QString relative = safeRelativePath(archive.nameOf(i));
        if (relative.isEmpty())
            continue;
        const QString path = folder + QLatin1Char('/') + relative;
        if (SzArEx_IsDir(&db, i)) {
            if (!QDir().mkpath(path))
                return QStringLiteral("%1 could not be made").arg(path);
            continue;
        }
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return QStringLiteral("%1 could not be made").arg(QFileInfo(path).absolutePath());
        // An empty file is written without the decoder, which would let go
        // of the block it holds for it.
        size_t offset = 0;
        size_t size = 0;
        if (SzArEx_GetFileSize(&db, i) > 0) {
            const SRes res = SzArEx_Extract(&db, &archive.look.vt, i, &blockIndex, &outBuffer, &outBufferSize, &offset,
                                            &size, &archive.alloc, &archive.allocTemp);
            if (res != SZ_OK)
                return describe(res);
        }
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QStringLiteral("%1 could not be written: %2").arg(path, out.errorString());
        const char *data = reinterpret_cast<const char *>(outBuffer + offset);
        if ((size > 0 && out.write(data, qint64(size)) != qint64(size)) || !out.flush())
            return QStringLiteral("%1 could not be written: %2").arg(path, out.errorString());
        out.close();
        written += qint64(size);
        if (progress && !progress(written, total))
            return QStringLiteral("Stopped");
    }
    return QString();
}

} // namespace sevenzip
