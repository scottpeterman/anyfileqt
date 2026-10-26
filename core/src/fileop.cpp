// core/src/fileop.cpp
#include "anyfile/fileop.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaMethod>
#include <QSet>

#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <climits>
#include <unistd.h>
#endif

#ifdef Q_OS_MACOS
#include <copyfile.h>
#endif

#include "anyfile/naming.h"

namespace anyfile {

namespace {

// Case-insensitive on the two platforms whose default filesystems are. Used
// only for "is this the same place" questions, never to build a path.
constexpr Qt::CaseSensitivity kPathCase =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    Qt::CaseInsensitive;
#else
    Qt::CaseSensitive;
#endif

qint64 nowMs() {
    static const QElapsedTimer t = [] {
        QElapsedTimer e;
        e.start();
        return e;
    }();
    return t.elapsed();
}

QString cleanAbs(const QString &p) {
    return QDir::cleanPath(QFileInfo(p).absoluteFilePath());
}

// Canonical when the path exists, so a symlinked or differently-cased route to
// the same directory compares equal. Falls back to the cleaned path.
QString canon(const QString &p) {
    const QString c = QFileInfo(p).canonicalFilePath();
    return c.isEmpty() ? cleanAbs(p) : c;
}

bool samePath(const QString &a, const QString &b) {
    return a.compare(b, kPathCase) == 0;
}

bool isInside(const QString &child, const QString &parent) {
    if (samePath(child, parent)) return true;
    const QString p = parent.endsWith(QLatin1Char('/')) ? parent : parent + QLatin1Char('/');
    return child.startsWith(p, kPathCase);
}

QString childPath(const QString &base, const QString &rel) {
    return rel.isEmpty() ? base : base + QLatin1Char('/') + rel;
}

// QFile::remove fails on a read-only file on Windows, and a copied tree full
// of read-only files is common there (anything that came off a CD or out of a
// source-control checkout). Clear the bit and try once more.
bool removeFile(const QString &path) {
    if (QFile::remove(path)) return true;
    const QFileInfo fi(path);
    if (!fi.isSymLink()) {
        QFile::setPermissions(path, fi.permissions() | QFileDevice::WriteOwner |
                                        QFileDevice::WriteUser);
    }
    return QFile::remove(path);
}

#ifdef Q_OS_WIN
// A directory that is a reparse point is a symlink or a junction. Recursing
// into one copies or deletes whatever it points at, which for a junction like
// "Application Data" is a loop back into the profile.
bool isReparseDir(const QString &path) {
    const DWORD a = GetFileAttributesW(
        reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()));
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) &&
           (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

// CopyFileExW honours MAX_PATH unless the path carries the \\?\ prefix, and
// deep trees on Windows pass 260 characters routinely. The prefix also turns
// off path normalisation, so it is only ever applied to an absolute, cleaned,
// backslashed path.
std::wstring longPath(const QString &path) {
    QString n = QDir::toNativeSeparators(cleanAbs(path));
    if (n.size() >= 240 && !n.startsWith(QLatin1String("\\\\?\\"))) {
        if (n.startsWith(QLatin1String("\\\\")))
            n = QLatin1String("\\\\?\\UNC\\") + n.mid(2);
        else
            n = QLatin1String("\\\\?\\") + n;
    }
    return n.toStdWString();
}

DWORD CALLBACK copyProgressThunk(LARGE_INTEGER, LARGE_INTEGER transferred, LARGE_INTEGER,
                                 LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE,
                                 LPVOID data) {
    auto *fn = static_cast<std::function<bool(qint64)> *>(data);
    return (*fn)(transferred.QuadPart) ? PROGRESS_CONTINUE : PROGRESS_CANCEL;
}
#endif

QString plural(int n, const char *one, const char *many) {
    return n == 1 ? QString::fromLatin1(one) : QString::fromLatin1(many).arg(n);
}

}  // namespace

FileJob::FileJob(OpKind kind, const QStringList &sources, const QString &dest,
                 QObject *parent)
    : QObject(parent), m_kind(kind), m_sources(sources), m_dest(dest) {}

QString FileJob::description() const {
    const QString what =
        m_sources.size() == 1
            ? QStringLiteral("\"%1\"").arg(QFileInfo(m_sources.first()).fileName())
            : plural(int(m_sources.size()), "1 item", "%1 items");
    QString to = QFileInfo(m_dest).fileName();
    if (to.isEmpty()) to = QDir::toNativeSeparators(m_dest);
    switch (m_kind) {
    case OpKind::Copy: return QStringLiteral("Copying %1 to %2").arg(what, to);
    case OpKind::Move: return QStringLiteral("Moving %1 to %2").arg(what, to);
    case OpKind::Trash:
        return QStringLiteral("Moving %1 to the %2").arg(what, trashDisplayName());
    case OpKind::Delete: return QStringLiteral("Deleting %1").arg(what);
    }
    return {};
}

// --- control, any thread ----------------------------------------------------

void FileJob::cancel() {
    m_cancel.store(true);
    QMutexLocker l(&m_mutex);
    m_cond.wakeAll();
}

void FileJob::setPaused(bool paused) {
    m_paused.store(paused);
    if (!paused) {
        QMutexLocker l(&m_mutex);
        m_cond.wakeAll();
    }
}

void FileJob::answerConflict(ConflictChoice choice, bool applyToAll) {
    QMutexLocker l(&m_mutex);
    m_answer = choice;
    m_applyAll = applyToAll;
    m_answered = true;
    m_cond.wakeAll();
}

// --- worker side ------------------------------------------------------------

bool FileJob::checkpoint() {
    if (m_cancel.load()) return false;
    if (m_paused.load()) {
        QMutexLocker l(&m_mutex);
        // A timed wait, so a cancel() that lands between the load above and
        // the wait below is still seen within one tick.
        while (m_paused.load() && !m_cancel.load()) m_cond.wait(&m_mutex, 200);
    }
    return !m_cancel.load();
}

void FileJob::addError(const QString &path, const QString &msg) {
    m_result.errors.append({path, msg});
}

void FileJob::emitProgress(bool force) {
    // Throttled to ten a second. A tree of small files otherwise posts one
    // queued event per file, and a hundred thousand of them queued faster
    // than the UI drains them is a progress bar that finishes a minute after
    // the copy did.
    const qint64 t = nowMs();
    if (!force && m_lastEmitMs >= 0 && t - m_lastEmitMs < m_progressIntervalMs) return;
    m_lastEmitMs = t;
    emit progress(m_progress);
}

ConflictChoice FileJob::askConflict(const QString &src, const QString &dst) {
    if (m_sticky) return *m_sticky;
    static const QMetaMethod sig = QMetaMethod::fromSignal(&FileJob::conflict);
    if (!isSignalConnected(sig)) return m_defaultChoice;

    const QFileInfo s(src), d(dst);
    ConflictInfo ci;
    ci.source = src;
    ci.target = dst;
    ci.sourceSize = s.size();
    ci.targetSize = d.size();
    ci.sourceModified = s.lastModified();
    ci.targetModified = d.lastModified();

    {
        QMutexLocker l(&m_mutex);
        m_answered = false;
    }
    // Emitted with the mutex NOT held. A direct connection answers inside
    // this call, on this thread, and answerConflict() takes the same mutex.
    emit conflict(ci);

    QMutexLocker l(&m_mutex);
    while (!m_answered && !m_cancel.load()) m_cond.wait(&m_mutex, 200);
    if (m_cancel.load()) return ConflictChoice::Cancel;
    if (m_applyAll && m_answer != ConflictChoice::Cancel) m_sticky = m_answer;
    return m_answer;
}

void FileJob::run() {
    m_result = {};
    m_progress = {};
    m_sticky.reset();

    if (m_kind == OpKind::Trash) {
        // No scan. Trashing is a rename of each top-level item, and walking a
        // large tree first only to count what is about to be renamed in one
        // call is the slowest possible way to do it.
        runTrash();
    } else {
        m_progress.phase = JobProgress::Scanning;
        emitProgress(true);
        if (scan()) {
            m_progress.phase = JobProgress::Working;
            emitProgress(true);
            if (m_kind == OpKind::Delete)
                runDelete();
            else
                runCopyOrMove();
        }
    }

    m_result.cancelled = m_cancel.load();
    m_result.itemsDone = m_progress.itemsDone;
    m_progress.phase = JobProgress::Done;
    m_progress.current.clear();
    emitProgress(true);
}

// --- scan -------------------------------------------------------------------

bool FileJob::scan() {
    m_tops.clear();
    for (const QString &raw : m_sources) {
        if (m_cancel.load()) return false;
        const QString src = cleanAbs(raw);
        if (!pathExists(src)) {
            addError(src, QStringLiteral("The item no longer exists"));
            continue;
        }
        Top top;
        top.source = src;
        scanInto(top, src, QString());
        m_progress.bytesTotal += top.bytes;
        m_tops.append(top);
    }
    return !m_cancel.load();
}

void FileJob::scanInto(Top &top, const QString &path, const QString &rel) {
    const QFileInfo fi(path);
    Entry e;
    e.source = path;
    e.rel = rel;

#ifdef Q_OS_WIN
    // .lnk shortcuts are ordinary files on Windows and are copied as bytes.
    // Directory symlinks and junctions are the links that matter.
    e.isLink = fi.isDir() && isReparseDir(path);
#else
    e.isLink = fi.isSymLink();
#endif

    ++m_progress.itemsTotal;
    if (e.isLink) {
        top.entries.append(e);
        return;
    }
    if (fi.isDir()) {
        e.isDir = true;
        top.entries.append(e);
        if (!fi.isReadable()) {
            addError(path, QStringLiteral("The folder could not be read"));
            return;
        }
        const QFileInfoList kids = QDir(path).entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
            QDir::Name);
        for (const QFileInfo &k : kids) {
            if (m_cancel.load()) return;
            scanInto(top, k.absoluteFilePath(),
                     rel.isEmpty() ? k.fileName() : rel + QLatin1Char('/') + k.fileName());
        }
        emitProgress(false);
        return;
    }
    e.size = fi.size();
    top.bytes += e.size;
    top.entries.append(e);
}

// --- copy and move ----------------------------------------------------------

void FileJob::runCopyOrMove() {
    const QString destDir = cleanAbs(m_dest);
    if (!QFileInfo(destDir).isDir()) {
        addError(destDir, QStringLiteral("The destination folder does not exist"));
        return;
    }
    const QString destCanon = canon(destDir);

    for (const Top &top : m_tops) {
        if (!checkpoint()) return;
        const QFileInfo sfi(top.source);
        const bool topIsDir = !top.entries.isEmpty() && top.entries.first().isDir;
        const QString name = sfi.fileName();
        const bool sameDir = samePath(canon(sfi.absolutePath()), destCanon);
        const int topItems = int(top.entries.size());

        // Moving something into the folder it is already in: Explorer does
        // nothing and says nothing, and so does this.
        if (m_kind == OpKind::Move && sameDir) {
            m_progress.itemsDone += topItems;
            skipBytes(top.bytes);
            continue;
        }
        if (topIsDir && isInside(destCanon, canon(top.source))) {
            addError(top.source,
                     QStringLiteral("The destination folder is inside the source folder"));
            m_progress.itemsDone += topItems;
            skipBytes(top.bytes);
            continue;
        }

        QString target = QDir(destDir).filePath(name);
        if (sameDir && m_kind == OpKind::Copy)
            target = QDir(destDir).filePath(copyName(destDir, name, topIsDir));

        // A move on one volume is a rename, whatever the size of the tree.
        // QDir::rename and NOT QFile::rename: when the rename fails, QFile
        // falls back to copying the file's contents itself, synchronously, with
        // no progress and no cancel -- so a cross-volume move of a 20GB file
        // would freeze inside this call. QDir::rename just fails, and the copy
        // below does it properly.
        if (m_kind == OpKind::Move && !pathExists(target) &&
            QDir().rename(top.source, target)) {
            m_progress.itemsDone += topItems;
            skipBytes(top.bytes);
            m_result.created.append(target);
            emitProgress(false);
            continue;
        }

        QStringList movedOk;
        if (!copyTop(top, target, m_kind == OpKind::Move ? &movedOk : nullptr)) return;

        if (m_kind == OpKind::Move) {
            // Only what was actually copied is removed. A skipped conflict
            // leaves its source where it was, and so does every folder above
            // it -- rmdir fails on a folder that is not empty, which is the
            // behaviour wanted, so its failure is not reported.
            for (const QString &f : movedOk) {
                if (!removeFile(f))
                    addError(f, QStringLiteral(
                                    "Copied, but the original could not be removed"));
            }
            for (qsizetype i = top.entries.size() - 1; i >= 0; --i) {
                const Entry &e = top.entries.at(i);
                if (e.isDir) QDir().rmdir(e.source);
            }
        }
    }
}

bool FileJob::copyTop(const Top &top, const QString &target, QStringList *movedOk) {
    const bool existed = pathExists(target);
    QString topWritten;
    QString skipRel;   // a folder that could not be created; its contents are skipped
    bool skipAll = false;

    for (const Entry &e : top.entries) {
        if (!checkpoint()) return false;

        if (skipAll || (!skipRel.isEmpty() &&
                        e.rel.startsWith(skipRel + QLatin1Char('/')))) {
            ++m_progress.itemsDone;
            skipBytes(e.size);
            continue;
        }

        const QString dst = childPath(target, e.rel);
        m_progress.current = e.source;

        if (e.isDir) {
            const QFileInfo dfi(dst);
            bool ok = true;
            if (dfi.isSymLink() || (dfi.exists() && !dfi.isDir())) {
                addError(e.source,
                         QStringLiteral("A file with this name already exists in the destination"));
                ok = false;
            } else if (!dfi.exists() && !QDir().mkpath(dst)) {
                addError(e.source, QStringLiteral("The folder could not be created"));
                ok = false;
            }
            if (!ok) {
                if (e.rel.isEmpty())
                    skipAll = true;
                else
                    skipRel = e.rel;
            }
            if (ok && e.rel.isEmpty()) topWritten = dst;
            ++m_progress.itemsDone;
            emitProgress(false);
            continue;
        }

        QString written;
        const FileOutcome o =
            e.isLink ? copyLink(e, dst, &written) : copyOneFile(e, dst, &written);
        if (o == FileOutcome::Cancelled) return false;
        if (o == FileOutcome::Done) {
            if (movedOk) movedOk->append(e.source);
            if (e.rel.isEmpty()) topWritten = written;
        }
        ++m_progress.itemsDone;
        emitProgress(false);
    }

    // "Created" means new in the destination: a merge into a folder that was
    // already there is not reported, a Keep-both rename of a top-level file is.
    if (!topWritten.isEmpty() && (!existed || topWritten != target))
        m_result.created.append(topWritten);
    return true;
}

FileJob::FileOutcome FileJob::copyOneFile(const Entry &e, const QString &target,
                                          QString *written) {
    QString dst = target;
    bool replace = false;

    if (pathExists(dst)) {
        const QFileInfo dfi(dst);
        if (dfi.isDir() && !dfi.isSymLink()) {
            addError(e.source,
                     QStringLiteral("A folder with this name already exists in the destination"));
            skipBytes(e.size);
            return FileOutcome::Failed;
        }
        if (samePath(canon(dst), canon(e.source))) {
            addError(e.source, QStringLiteral("The source and destination are the same file"));
            skipBytes(e.size);
            return FileOutcome::Failed;
        }
        switch (askConflict(e.source, dst)) {
        case ConflictChoice::Cancel:
            cancel();
            return FileOutcome::Cancelled;
        case ConflictChoice::Skip:
            skipBytes(e.size);
            return FileOutcome::Skipped;
        case ConflictChoice::KeepBoth: {
            const QString dir = dfi.absolutePath();
            dst = QDir(dir).filePath(keepBothName(dir, dfi.fileName(), false));
            break;
        }
        case ConflictChoice::Replace:
            replace = true;
            break;
        }
    }

    // Written beside the destination under a temporary name and renamed into
    // place at the end. A cancelled or failed copy then leaves nothing behind,
    // and Replace never destroys the existing file before the new one is
    // complete -- copying straight onto it would leave half of each.
    const QFileInfo dfi(dst);
    const QString tmp = QDir(dfi.absolutePath())
                            .filePath(QStringLiteral(".~%1.anyfile-part").arg(dfi.fileName()));
    if (pathExists(tmp)) removeFile(tmp);

    const FileOutcome o = writeFile(e.source, tmp, e.size);
    if (o != FileOutcome::Done) {
        removeFile(tmp);
        return o;
    }
    if (replace && !removeFile(dst)) {
        removeFile(tmp);
        addError(dst, QStringLiteral("The existing file could not be replaced"));
        return FileOutcome::Failed;
    }
    if (!QDir().rename(tmp, dst)) {
        removeFile(tmp);
        addError(dst, QStringLiteral("The copy could not be renamed into place"));
        return FileOutcome::Failed;
    }
    if (written) *written = dst;
    return FileOutcome::Done;
}

FileJob::FileOutcome FileJob::writeFile(const QString &src, const QString &tmp, qint64 size) {
#ifdef Q_OS_WIN
    // CopyFileExW rather than a read/write loop. On a local disk the two are
    // close; on an SMB share they are not -- CopyFileEx asks the server to do
    // the copy itself when source and destination are on the same share, and
    // the loop drags every byte down to this machine and back up again. It also
    // carries the attributes, the timestamps and the alternate data streams,
    // which is where the Mark of the Web lives.
    //
    // NOT VERIFIED ON WINDOWS: written against the documented API.
    qint64 last = 0;
    std::function<bool(qint64)> onChunk = [&](qint64 transferred) {
        m_progress.bytesDone += transferred - last;
        last = transferred;
        emitProgress(false);
        // Blocks here while paused, inside CopyFileEx's own callback, which
        // is what pausing a CopyFileEx copy has to look like.
        return checkpoint();
    };
    BOOL cancelFlag = FALSE;
    const std::wstring s = longPath(src);
    const std::wstring d = longPath(tmp);
    if (!CopyFileExW(s.c_str(), d.c_str(), &copyProgressThunk, &onChunk, &cancelFlag,
                     COPY_FILE_FAIL_IF_EXISTS)) {
        const DWORD err = GetLastError();
        m_progress.bytesDone += size - last;
        if (err == ERROR_REQUEST_ABORTED || m_cancel.load()) return FileOutcome::Cancelled;
        addError(src, qt_error_string(int(err)));
        return FileOutcome::Failed;
    }
    m_progress.bytesDone += size - last;
    return FileOutcome::Done;
#else
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        addError(src, in.errorString());
        skipBytes(size);
        return FileOutcome::Failed;
    }
    QFile out(tmp);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        addError(src, QStringLiteral("Could not write to the destination: ") + out.errorString());
        skipBytes(size);
        return FileOutcome::Failed;
    }

    QByteArray buf;
    buf.resize(qsizetype(m_chunk));
    qint64 copied = 0;
    for (;;) {
        if (!checkpoint()) return FileOutcome::Cancelled;
        const qint64 n = in.read(buf.data(), m_chunk);
        if (n < 0) {
            addError(src, in.errorString());
            skipBytes(size - copied);
            return FileOutcome::Failed;
        }
        if (n == 0) break;
        if (out.write(buf.constData(), n) != n) {
            // Disk full lands here, and errorString() says so in the
            // platform's own words.
            addError(src, QStringLiteral("Write failed: ") + out.errorString());
            skipBytes(size - copied);
            return FileOutcome::Failed;
        }
        copied += n;
        m_progress.bytesDone += n;
        emitProgress(false);
    }
    // A file that grew or shrank while it was being copied: keep the running
    // total consistent with the scan's total rather than drifting past it.
    m_progress.bytesDone += size - copied;

    if (!out.flush()) {
        addError(src, QStringLiteral("Write failed: ") + out.errorString());
        return FileOutcome::Failed;
    }
    out.setPermissions(in.permissions());
    // Set while the file is still open and after the last write; any write
    // after this would move the time again.
    out.setFileTime(QFileInfo(src).lastModified(), QFileDevice::FileModificationTime);
    out.close();

#ifdef Q_OS_MACOS
    // Extended attributes and ACLs: Finder tags, the comment field, and
    // com.apple.quarantine. Dropping quarantine is not cosmetic -- an app
    // downloaded from the web and copied with a plain read/write loop loses
    // the flag that makes Gatekeeper check it on first launch. Failure is not
    // fatal: exFAT and most network shares take no xattrs, and the data is
    // already there.
    copyfile(QFile::encodeName(src).constData(), QFile::encodeName(tmp).constData(),
             nullptr, COPYFILE_SECURITY | COPYFILE_XATTR | COPYFILE_STAT);
#endif
    return FileOutcome::Done;
#endif
}

FileJob::FileOutcome FileJob::copyLink(const Entry &e, const QString &target,
                                       QString *written) {
#ifdef Q_OS_WIN
    Q_UNUSED(target);
    Q_UNUSED(written);
    addError(e.source, QStringLiteral("Skipped: folder links and junctions are not copied"));
    return FileOutcome::Failed;
#else
    // The link text as written, via readlink(2). QFile::symLinkTarget()
    // returns the RESOLVED absolute target, which turns every relative link in
    // a copied tree into an absolute one pointing back into the original.
    QByteArray text(PATH_MAX, '\0');
    const ssize_t n = ::readlink(QFile::encodeName(e.source).constData(), text.data(),
                                 size_t(text.size()) - 1);
    if (n < 0) {
        addError(e.source, QStringLiteral("The link could not be read"));
        return FileOutcome::Failed;
    }
    text.truncate(qsizetype(n));

    QString dst = target;
    if (pathExists(dst)) {
        const QFileInfo dfi(dst);
        if (dfi.isDir() && !dfi.isSymLink()) {
            addError(e.source,
                     QStringLiteral("A folder with this name already exists in the destination"));
            return FileOutcome::Failed;
        }
        switch (askConflict(e.source, dst)) {
        case ConflictChoice::Cancel:
            cancel();
            return FileOutcome::Cancelled;
        case ConflictChoice::Skip:
            return FileOutcome::Skipped;
        case ConflictChoice::KeepBoth: {
            const QString dir = dfi.absolutePath();
            dst = QDir(dir).filePath(keepBothName(dir, dfi.fileName(), false));
            break;
        }
        case ConflictChoice::Replace:
            if (!removeFile(dst)) {
                addError(dst, QStringLiteral("The existing file could not be replaced"));
                return FileOutcome::Failed;
            }
            break;
        }
    }
    if (::symlink(text.constData(), QFile::encodeName(dst).constData()) != 0) {
        addError(e.source, QStringLiteral("The link could not be created"));
        return FileOutcome::Failed;
    }
    if (written) *written = dst;
    return FileOutcome::Done;
#endif
}

// --- trash and delete -------------------------------------------------------

void FileJob::runTrash() {
    m_progress.phase = JobProgress::Working;
    m_progress.itemsTotal = int(m_sources.size());
    emitProgress(true);
    for (const QString &raw : m_sources) {
        if (!checkpoint()) return;
        const QString src = cleanAbs(raw);
        m_progress.current = src;
        if (!pathExists(src)) {
            addError(src, QStringLiteral("The item no longer exists"));
        } else if (!QFile::moveToTrash(src)) {
            addError(src, QStringLiteral("Could not move this to the %1. The drive it is "
                                         "on may not have one.")
                              .arg(trashDisplayName()));
        }
        ++m_progress.itemsDone;
        emitProgress(false);
    }
}

void FileJob::runDelete() {
    for (const Top &top : m_tops) {
        // Every ancestor of something that could not be deleted. Its rmdir is
        // certain to fail with "not empty", and reporting that on top of the
        // real failure buries the one line that matters.
        QSet<QString> blocked;
        const auto block = [&](const QString &path) {
            QString p = QFileInfo(path).absolutePath();
            while (p.size() >= top.source.size()) {
                blocked.insert(p);
                const QString up = QFileInfo(p).absolutePath();
                if (up == p) break;
                p = up;
            }
        };

        for (qsizetype i = top.entries.size() - 1; i >= 0; --i) {
            if (!checkpoint()) return;
            const Entry &e = top.entries.at(i);
            m_progress.current = e.source;
            bool ok;
            if (e.isDir) {
                if (blocked.contains(e.source)) {
                    ++m_progress.itemsDone;
                    continue;
                }
                ok = QDir().rmdir(e.source);
            } else if (e.isLink) {
#ifdef Q_OS_WIN
                // A junction or directory symlink: RemoveDirectory removes the
                // link and leaves what it points at alone.
                ok = QDir().rmdir(e.source);
#else
                ok = QFile::remove(e.source);
#endif
            } else {
                ok = removeFile(e.source);
                m_progress.bytesDone += e.size;
            }
            if (!ok) {
                addError(e.source, QStringLiteral("Could not be deleted"));
                block(e.source);
            }
            ++m_progress.itemsDone;
            emitProgress(false);
        }
    }
}

}  // namespace anyfile
