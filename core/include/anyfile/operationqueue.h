// core/include/anyfile/operationqueue.h
//
// Runs FileJobs one at a time, each on its own worker thread, in the order
// they were queued.
//
// ONE AT A TIME, not in parallel the way Explorer does it. Two copies to the
// same spinning disk, or across the same SMB link, run slower together than
// one after the other, and the progress of either means nothing while they
// fight. TeraCopy and Total Commander queue for the same reason. A second
// queue per destination volume is the obvious extension if it is ever wanted.
//
// The queue lives in the UI thread. A job is deleted (deleteLater) after
// jobFinished() has been emitted for it, so a receiver may use the pointer
// for the duration of that slot and not after.
//
// Destroying the queue cancels everything and WAITS for the running worker.
// That cannot deadlock: FileJob's conflict wait wakes on cancel() -- see the
// note at the top of fileop.h.
#pragma once

#include <QList>
#include <QObject>
#include <QStringList>

#include "anyfile/fileop.h"

class QThread;

namespace anyfile {

class OperationQueue : public QObject {
    Q_OBJECT
public:
    explicit OperationQueue(QObject *parent = nullptr);
    ~OperationQueue() override;

    // Returns the job, already queued, so the caller can connect to its
    // signals. It starts on the next turn of the event loop at the earliest,
    // never inside this call -- connecting right after enqueue() cannot miss
    // the first progress() or conflict().
    FileJob *enqueue(OpKind kind, const QStringList &sources, const QString &dest);

    FileJob *current() const { return m_current; }
    int waiting() const { return int(m_waiting.size()); }
    bool isBusy() const { return m_current || !m_waiting.isEmpty(); }

    void cancelAll();

signals:
    void jobStarted(anyfile::FileJob *job);
    void jobFinished(anyfile::FileJob *job, const anyfile::JobResult &result);
    void idle();

private:
    void startNext();
    void onThreadFinished();

    QList<FileJob *> m_waiting;
    FileJob *m_current = nullptr;
    QThread *m_thread = nullptr;
    bool m_shuttingDown = false;
};

}  // namespace anyfile
