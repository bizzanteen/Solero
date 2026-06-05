#pragma once
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>
#include <QColor>
#include <QSvgRenderer>
namespace solero {
inline QIcon redCrossIcon(int px = 26) {
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#e74c3c")); pen.setWidthF(px * 0.12); pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    double m = px * 0.28;
    p.drawLine(QPointF(m, m), QPointF(px - m, px - m));
    p.drawLine(QPointF(px - m, m), QPointF(m, px - m));
    return QIcon(pm);
}
inline QIcon redBangIcon(int px = 26) {
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Red filled circle with a white "!" in the middle.
    const QColor red("#e74c3c");
    p.setPen(Qt::NoPen);
    p.setBrush(red);
    double m = px * 0.10;
    p.drawEllipse(QRectF(m, m, px - 2 * m, px - 2 * m));
    QPen pen(Qt::white); pen.setWidthF(px * 0.13); pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    double cx = px / 2.0;
    p.drawLine(QPointF(cx, px * 0.28), QPointF(cx, px * 0.62));
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(cx, px * 0.76), px * 0.07, px * 0.07);
    return QIcon(pm);
}
inline QIcon renderSvgIcon(const QString& resPath, const QColor& tint, int px = 20) {
    if (resPath.isEmpty()) return {};
    QSvgRenderer r(resPath);
    if (!r.isValid()) return {};
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    { QPainter p(&pm); r.render(&p); }
    if (tint.isValid()) {
        QPainter cp(&pm);
        cp.setCompositionMode(QPainter::CompositionMode_SourceIn);
        cp.fillRect(pm.rect(), tint);
    }
    return QIcon(pm);
}
inline QColor contrastText(const QColor& bg) {
    // Perceived luminance (0..255); dark bg -> white text, light bg -> black text.
    double l = 0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue();
    return l > 140 ? QColor(Qt::black) : QColor(Qt::white);
}
}
