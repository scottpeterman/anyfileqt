// anyfileqt/app/icons.cpp
#include "app/icons.h"

#include <QGuiApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>

namespace anyfile {

namespace {

class GlyphEngine : public QIconEngine {
public:
    explicit GlyphEngine(Glyph g) : m_glyph(g) {}

    QIconEngine *clone() const override { return new GlyphEngine(m_glyph); }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1.0);
    }

    // Asked for by Qt with the screen's device pixel ratio, so a 2x Retina
    // display gets a 2x drawing rather than a 1x one scaled up.
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override {
        QPixmap pm(size * scale);
        pm.fill(Qt::transparent);
        pm.setDevicePixelRatio(scale);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        const QPalette pal = QGuiApplication::palette();
        const QColor ink = mode == QIcon::Disabled ? pal.color(QPalette::Disabled, QPalette::WindowText)
                           : mode == QIcon::Selected ? pal.color(QPalette::Active, QPalette::HighlightedText)
                                                     : pal.color(QPalette::Active, QPalette::WindowText);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        // A square in the middle of rect, mapped onto a 24x24 design grid.
        const qreal side = qMin(rect.width(), rect.height());
        p->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
        p->scale(side / 24.0, side / 24.0);
        QPen pen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        draw(p, ink);
        p->restore();
    }

private:
    void draw(QPainter *p, const QColor &ink) const {
        switch (m_glyph) {
        case Glyph::NewFolder: {
            QPainterPath f;
            f.moveTo(3, 7);
            f.lineTo(3, 19);
            f.lineTo(21, 19);
            f.lineTo(21, 9);
            f.lineTo(11, 9);
            f.lineTo(9, 7);
            f.closeSubpath();
            p->drawPath(f);
            p->drawLine(QPointF(15.5, 11.5), QPointF(15.5, 16.5));
            p->drawLine(QPointF(13, 14), QPointF(18, 14));
            break;
        }
        case Glyph::Cut:
            p->drawEllipse(QPointF(7, 17.5), 2.6, 2.6);
            p->drawEllipse(QPointF(17, 17.5), 2.6, 2.6);
            p->drawLine(QPointF(8.8, 15.6), QPointF(16.5, 3.5));
            p->drawLine(QPointF(15.2, 15.6), QPointF(7.5, 3.5));
            break;
        case Glyph::Copy:
            p->drawPolyline(QPolygonF({QPointF(4.5, 16), QPointF(4.5, 3.5), QPointF(15, 3.5)}));
            p->drawRoundedRect(QRectF(8.5, 7.5, 11.5, 13.5), 1.5, 1.5);
            break;
        case Glyph::Paste:
            p->drawRoundedRect(QRectF(5, 4.5, 14, 16.5), 2, 2);
            p->setBrush(ink);
            p->drawRoundedRect(QRectF(9, 3, 6, 3.4), 1, 1);
            p->setBrush(Qt::NoBrush);
            p->drawLine(QPointF(8.5, 11.5), QPointF(15.5, 11.5));
            p->drawLine(QPointF(8.5, 15.5), QPointF(13.5, 15.5));
            break;
        case Glyph::Rename:
            p->drawRoundedRect(QRectF(2.5, 7.5, 19, 9), 1.5, 1.5);
            p->drawLine(QPointF(6, 12), QPointF(10, 12));
            p->drawLine(QPointF(14, 4.5), QPointF(14, 19.5));
            p->drawLine(QPointF(12, 4.5), QPointF(16, 4.5));
            p->drawLine(QPointF(12, 19.5), QPointF(16, 19.5));
            break;
        case Glyph::Delete:
            p->drawLine(QPointF(4, 6.5), QPointF(20, 6.5));
            p->drawPolyline(QPolygonF({QPointF(9, 6.5), QPointF(9, 4), QPointF(15, 4), QPointF(15, 6.5)}));
            p->drawPolyline(QPolygonF({QPointF(6, 6.5), QPointF(7, 20.5), QPointF(17, 20.5), QPointF(18, 6.5)}));
            p->drawLine(QPointF(10, 10), QPointF(10, 17));
            p->drawLine(QPointF(14, 10), QPointF(14, 17));
            break;
        case Glyph::Details:
            for (qreal y : {6.0, 12.0, 18.0}) {
                p->setBrush(ink);
                p->drawRect(QRectF(3, y - 1.1, 2.2, 2.2));
                p->setBrush(Qt::NoBrush);
                p->drawLine(QPointF(8, y), QPointF(21, y));
            }
            break;
        case Glyph::Icons:
            for (const QRectF &r : {QRectF(4, 4, 7, 7), QRectF(13, 4, 7, 7), QRectF(4, 13, 7, 7),
                                    QRectF(13, 13, 7, 7)})
                p->drawRoundedRect(r, 1.2, 1.2);
            break;
        case Glyph::Hidden: {
            QPainterPath eye;
            eye.moveTo(2, 12);
            eye.quadTo(12, 2.5, 22, 12);
            eye.quadTo(12, 21.5, 2, 12);
            p->drawPath(eye);
            p->drawEllipse(QPointF(12, 12), 3, 3);
            break;
        }
        case Glyph::Dual:
            p->drawRoundedRect(QRectF(3, 5, 18, 14), 1.5, 1.5);
            p->drawLine(QPointF(12, 5), QPointF(12, 19));
            break;
        case Glyph::Properties:
            p->drawEllipse(QPointF(12, 12), 9, 9);
            p->setBrush(ink);
            p->drawEllipse(QPointF(12, 7.6), 0.9, 0.9);
            p->setBrush(Qt::NoBrush);
            p->drawLine(QPointF(12, 11), QPointF(12, 17));
            break;
        case Glyph::Terminal:
            p->drawRoundedRect(QRectF(3, 5, 18, 14), 1.5, 1.5);
            p->drawPolyline(QPolygonF({QPointF(7, 9.5), QPointF(10, 12), QPointF(7, 14.5)}));
            p->drawLine(QPointF(12, 15), QPointF(16, 15));
            break;
        }
    }

    Glyph m_glyph;
};

}  // namespace

QIcon glyphIcon(Glyph g) { return QIcon(new GlyphEngine(g)); }

}  // namespace anyfile
