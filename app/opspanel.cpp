// app/opspanel.cpp
#include "app/opspanel.h"

#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>

#include "anyfile/fsinfo.h"
#include "anyfile/operationqueue.h"
#include "app/conflictdialog.h"

namespace anyfile {

namespace {


QString formatEta(double seconds) {
    if (seconds < 60) return QObject::tr("about %1 seconds left").arg(qMax(1, int(seconds)));
    if (seconds < 3600) return QObject::tr("about %1 minutes left").arg(int(seconds / 60 + 0.5));
    return QObject::tr("about %1 hours left").arg(QString::number(seconds / 3600, 'f', 1));
}

}  // namespace

OperationsPanel::OperationsPanel(OperationQueue *queue, QWidget *parent)
    : QFrame(parent), m_queue(queue) {
    setFrameShape(QFrame::StyledPanel);
    setVisible(false);

    m_title = new QLabel(this);
    QFont tf = m_title->font();
    tf.setBold(true);
    m_title->setFont(tf);
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 1000);
    m_bar->setTextVisible(false);
    m_detail = new QLabel(this);
    m_current = new QLabel(this);
    m_current->setMinimumWidth(100);
    m_queued = new QLabel(this);
    m_pause = new QPushButton(tr("Pause"), this);
    m_cancel = new QPushButton(tr("Cancel"), this);

    auto *g = new QGridLayout(this);
    g->setContentsMargins(10, 6, 10, 6);
    g->addWidget(m_title, 0, 0, 1, 2);
    g->addWidget(m_queued, 0, 2, Qt::AlignRight);
    g->addWidget(m_bar, 1, 0, 1, 3);
    g->addWidget(m_pause, 1, 3);
    g->addWidget(m_cancel, 1, 4);
    g->addWidget(m_detail, 2, 0);
    g->addWidget(m_current, 2, 1, 1, 2);
    g->setColumnStretch(1, 1);

    m_showTimer = new QTimer(this);
    m_showTimer->setSingleShot(true);
    m_showTimer->setInterval(300);
    connect(m_showTimer, &QTimer::timeout, this, [this] {
        if (m_job) setVisible(true);
    });

    connect(m_pause, &QPushButton::clicked, this, [this] {
        if (!m_job) return;
        const bool p = !m_job->isPaused();
        m_job->setPaused(p);
        m_pause->setText(p ? tr("Resume") : tr("Pause"));
        if (!p) {
            // Speed is measured from here again, not averaged across the pause.
            m_lastMs = m_clock.elapsed();
        }
    });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (m_job) m_job->cancel();
    });

    connect(queue, &OperationQueue::jobStarted, this, &OperationsPanel::onStarted);
    connect(queue, &OperationQueue::jobFinished, this, [this] { onFinished(); });
}

void OperationsPanel::onStarted(FileJob *job) {
    m_job = job;
    m_title->setText(job->description());
    m_detail->clear();
    m_current->clear();
    m_bar->setRange(0, 0);  // busy until the scan has a total
    m_pause->setText(tr("Pause"));
    m_rate = 0;
    m_lastBytes = 0;
    m_clock.restart();
    m_lastMs = 0;
    updateQueued();
    connect(job, &FileJob::progress, this, &OperationsPanel::onProgress);
    connect(job, &FileJob::conflict, this, &OperationsPanel::onConflict);
    if (!isVisible()) m_showTimer->start();
}

void OperationsPanel::onProgress(const JobProgress &p) {
    if (!m_job) return;
    updateQueued();
    if (p.phase == JobProgress::Scanning) {
        m_bar->setRange(0, 0);
        m_detail->setText(tr("Counting %1 items...").arg(p.itemsTotal));
        return;
    }
    m_bar->setRange(0, 1000);

    const bool byBytes = p.bytesTotal > 0 &&
                         (m_job->kind() == OpKind::Copy || m_job->kind() == OpKind::Move);
    if (byBytes) {
        const qint64 done = qMin(p.bytesDone, p.bytesTotal);
        m_bar->setValue(int(done * 1000 / p.bytesTotal));

        const qint64 now = m_clock.elapsed();
        if (now - m_lastMs >= 250 && !m_job->isPaused()) {
            const double inst = double(done - m_lastBytes) * 1000.0 / double(now - m_lastMs);
            // Exponential smoothing. The raw figure swings wildly across a
            // tree of small files, where per-file overhead dominates.
            m_rate = m_rate <= 0 ? inst : 0.7 * m_rate + 0.3 * inst;
            m_lastMs = now;
            m_lastBytes = done;
        }
        QString d = tr("%1 of %2").arg(humanSize(done), humanSize(p.bytesTotal));
        if (m_job->isPaused()) {
            d += QStringLiteral("  -  ") + tr("paused");
        } else if (m_rate > 1) {
            d += QStringLiteral("  -  %1/s  -  ").arg(humanSize(qint64(m_rate)));
            d += formatEta(double(p.bytesTotal - done) / m_rate);
        }
        m_detail->setText(d);
    } else {
        const int total = qMax(1, p.itemsTotal);
        m_bar->setValue(int(qint64(qMin(p.itemsDone, total)) * 1000 / total));
        m_detail->setText(tr("%1 of %2 items").arg(p.itemsDone).arg(p.itemsTotal));
    }

    const QString name = QFileInfo(p.current).fileName();
    m_current->setText(m_current->fontMetrics().elidedText(name, Qt::ElideMiddle,
                                                          qMax(100, m_current->width())));
}

void OperationsPanel::onConflict(const ConflictInfo &info) {
    QPointer<FileJob> job = m_job;
    if (!job) return;
    if (m_haveAutoAnswer) {
        job->answerConflict(m_autoAnswer, true);
        return;
    }
    setVisible(true);
    ConflictDialog dlg(info, window());
    dlg.exec();
    // The job cannot have been deleted while the dialog was open -- it is
    // blocked waiting for this answer -- but the pointer is checked anyway.
    if (job) job->answerConflict(dlg.choice(), dlg.applyToAll());
}

void OperationsPanel::onFinished() {
    m_job = nullptr;
    if (!m_queue->isBusy()) {
        m_showTimer->stop();
        setVisible(false);
    }
}

void OperationsPanel::updateQueued() {
    const int n = m_queue->waiting();
    m_queued->setText(n == 0 ? QString() : n == 1 ? tr("1 more queued") : tr("%1 more queued").arg(n));
}

}  // namespace anyfile
