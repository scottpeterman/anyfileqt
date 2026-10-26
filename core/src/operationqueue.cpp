// core/src/operationqueue.cpp
#include "anyfile/operationqueue.h"

#include <QThread>
#include <QTimer>

namespace anyfile {

OperationQueue::OperationQueue(QObject *parent) : QObject(parent) {
    qRegisterMetaType<anyfile::JobProgress>();
    qRegisterMetaType<anyfile::ConflictInfo>();
    qRegisterMetaType<anyfile::JobResult>();
}

OperationQueue::~OperationQueue() {
    m_shuttingDown = true;
    cancelAll();
    if (m_thread) {
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    delete m_current;
    qDeleteAll(m_waiting);
}

FileJob *OperationQueue::enqueue(OpKind kind, const QStringList &sources,
                                 const QString &dest) {
    auto *job = new FileJob(kind, sources, dest, this);
    m_waiting.append(job);
    // Queued, so the caller's connect() after this call lands first.
    if (!m_current) QTimer::singleShot(0, this, &OperationQueue::startNext);
    return job;
}

void OperationQueue::cancelAll() {
    for (FileJob *j : m_waiting) j->cancel();
    if (m_current) m_current->cancel();
}

void OperationQueue::startNext() {
    if (m_current || m_shuttingDown) return;
    if (m_waiting.isEmpty()) {
        emit idle();
        return;
    }
    m_current = m_waiting.takeFirst();
    FileJob *job = m_current;
    m_thread = QThread::create([job] { job->run(); });
    m_thread->setObjectName(QStringLiteral("anyfile-job"));
    // QThread::finished, not a signal from the job: it is emitted after run()
    // has returned and the thread is done with the job, so deleting the job in
    // response is safe. A "done" signal emitted from inside run() would arrive
    // while the worker could still be touching it.
    connect(m_thread, &QThread::finished, this, &OperationQueue::onThreadFinished);
    emit jobStarted(job);
    m_thread->start();
}

void OperationQueue::onThreadFinished() {
    FileJob *job = m_current;
    m_current = nullptr;
    if (m_thread) {
        m_thread->deleteLater();
        m_thread = nullptr;
    }
    if (job) {
        emit jobFinished(job, job->result());
        job->deleteLater();
    }
    startNext();
}

}  // namespace anyfile
