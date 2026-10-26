// core/include/anyfile/fileop.h
//
// One file operation -- copy, move, trash or delete -- run to completion on
// whatever thread calls run(). OperationQueue gives each job a worker thread;
// the tests call run() directly.
//
// THREADING. A FileJob lives in the UI thread (that is where it was created)
// and run() executes on a worker. Its signals are emitted from the worker, so
// every connection to a UI object is queued by Qt automatically. cancel(),
// setPaused() and answerConflict() may be called from any thread.
//
// THE CONFLICT HANDSHAKE IS A WAIT CONDITION, NOT A BlockingQueuedConnection.
// The blocking connection is less code and it deadlocks the moment the UI
// thread waits on the worker -- which is exactly what quitting with a job in
// flight does: the UI thread blocks in QThread::wait(), the worker blocks in
// the blocking emit waiting for the UI thread, and neither moves. Here the
// worker waits on m_cond with a timeout and re-checks m_cancel, so cancel()
// always gets it out.
//
// If nothing is connected to conflict(), the job does not wait at all: it
// applies the default choice (Skip, unless setDefaultConflictChoice() said
// otherwise). A job that asks a question nobody is listening for would hang
// forever and look exactly like a slow copy.
#pragma once

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QWaitCondition>

#include <atomic>
#include <optional>

namespace anyfile {

enum class OpKind { Copy, Move, Trash, Delete };

enum class ConflictChoice { Replace, Skip, KeepBoth, Cancel };

struct ConflictInfo {
    QString source;
    QString target;
    qint64 sourceSize = 0;
    qint64 targetSize = 0;
    QDateTime sourceModified;
    QDateTime targetModified;
};

struct JobProgress {
    enum Phase { Scanning, Working, Done } phase = Scanning;
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;
    int itemsDone = 0;
    int itemsTotal = 0;
    QString current;  // the file being worked on, absolute path
};

struct JobError {
    QString path;
    QString message;
};

struct JobResult {
    bool cancelled = false;
    int itemsDone = 0;
    QList<JobError> errors;
    QStringList created;  // top-level paths created in the destination
};

class FileJob : public QObject {
    Q_OBJECT
public:
    // dest is the destination DIRECTORY for Copy and Move, and ignored for
    // Trash and Delete.
    FileJob(OpKind kind, const QStringList &sources, const QString &dest,
            QObject *parent = nullptr);

    OpKind kind() const { return m_kind; }
    const QStringList &sources() const { return m_sources; }
    const QString &destination() const { return m_dest; }
    QString description() const;

    // Blocking. Call it once.
    void run();
    const JobResult &result() const { return m_result; }

    void cancel();
    void setPaused(bool paused);
    bool isPaused() const { return m_paused.load(); }
    bool isCancelled() const { return m_cancel.load(); }

    void answerConflict(ConflictChoice choice, bool applyToAll);
    void setDefaultConflictChoice(ConflictChoice choice) { m_defaultChoice = choice; }

    // The read/write buffer for the portable copy loop. Only the tests change
    // it, to make a small file take many chunks.
    void setChunkSize(qint64 bytes) { m_chunk = bytes; }
    // Minimum gap between progress() emissions; 100ms unless a test needs
    // every chunk reported.
    void setProgressInterval(int ms) { m_progressIntervalMs = ms; }

signals:
    void progress(const anyfile::JobProgress &p);
    void conflict(const anyfile::ConflictInfo &info);

private:
    struct Entry {
        QString source;   // absolute
        QString rel;      // relative to the top item, '/'-separated; empty for the top
        bool isDir = false;
        bool isLink = false;
        qint64 size = 0;
    };
    struct Top {
        QString source;
        QList<Entry> entries;  // pre-order: a directory comes before its contents
        qint64 bytes = 0;
    };

    bool scan();
    void scanInto(Top &top, const QString &path, const QString &rel);
    void runCopyOrMove();
    void runTrash();
    void runDelete();

    // false = the job was cancelled, stop everything.
    bool copyTop(const Top &top, const QString &target, QStringList *movedOk);
    enum class FileOutcome { Done, Skipped, Failed, Cancelled };
    // written receives the path actually written, which differs from target
    // after a Keep-both rename.
    FileOutcome copyOneFile(const Entry &e, const QString &target, QString *written);
    FileOutcome writeFile(const QString &src, const QString &tmp, qint64 size);
    FileOutcome copyLink(const Entry &e, const QString &target, QString *written);
    ConflictChoice askConflict(const QString &src, const QString &dst);

    bool checkpoint();
    void addError(const QString &path, const QString &msg);
    void emitProgress(bool force);
    void skipBytes(qint64 n) { m_progress.bytesDone += n; }

    OpKind m_kind;
    QStringList m_sources;
    QString m_dest;
    QList<Top> m_tops;
    JobResult m_result;
    JobProgress m_progress;
    qint64 m_chunk = 1 << 20;
    qint64 m_lastEmitMs = -1;
    int m_progressIntervalMs = 100;

    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_paused{false};
    QMutex m_mutex;
    QWaitCondition m_cond;
    bool m_answered = false;
    ConflictChoice m_answer = ConflictChoice::Skip;
    bool m_applyAll = false;
    std::optional<ConflictChoice> m_sticky;
    ConflictChoice m_defaultChoice = ConflictChoice::Skip;
};

}  // namespace anyfile

Q_DECLARE_METATYPE(anyfile::JobProgress)
Q_DECLARE_METATYPE(anyfile::ConflictInfo)
Q_DECLARE_METATYPE(anyfile::JobResult)
