// app/opspanel.h
//
// The strip along the bottom of the window while a file operation runs: what
// it is doing, a progress bar, bytes / speed / time left, the current file,
// Pause and Cancel, and how many more are queued.
//
// It appears only for an operation still running after 300ms. A rename-speed
// move or a two-file copy would otherwise flash a panel open and shut, which
// reads as something having gone wrong.
//
// It also answers conflicts: a job's conflict() arrives here, the dialog runs,
// and the answer goes back through FileJob::answerConflict().
#pragma once

#include <QElapsedTimer>
#include <QFrame>
#include <QPointer>

#include "anyfile/fileop.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

namespace anyfile {

class OperationQueue;

class OperationsPanel : public QFrame {
    Q_OBJECT
public:
    explicit OperationsPanel(OperationQueue *queue, QWidget *parent = nullptr);

    // For the tests: answers conflicts without a dialog when set.
    void setAutoAnswer(ConflictChoice choice) {
        m_autoAnswer = choice;
        m_haveAutoAnswer = true;
    }

private:
    void onStarted(FileJob *job);
    void onProgress(const JobProgress &p);
    void onConflict(const ConflictInfo &info);
    void onFinished();
    void updateQueued();

    OperationQueue *m_queue;
    QPointer<FileJob> m_job;
    QLabel *m_title = nullptr;
    QLabel *m_detail = nullptr;
    QLabel *m_current = nullptr;
    QLabel *m_queued = nullptr;
    QProgressBar *m_bar = nullptr;
    QPushButton *m_pause = nullptr;
    QPushButton *m_cancel = nullptr;
    QTimer *m_showTimer = nullptr;

    QElapsedTimer m_clock;
    qint64 m_lastMs = 0;
    qint64 m_lastBytes = 0;
    double m_rate = 0;  // bytes per second, smoothed

    bool m_haveAutoAnswer = false;
    ConflictChoice m_autoAnswer = ConflictChoice::Skip;
};

}  // namespace anyfile
