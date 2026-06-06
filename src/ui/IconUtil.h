#pragma once
#include <QIcon>
#include <QList>
#include <QPixmap>
#include <QPainter>
#include <QPen>
#include <QPainterPath>
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
inline QIcon yellowUpArrowIcon(int px = 16) {
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor yellow("#f1c40f");
    p.setPen(Qt::NoPen);
    p.setBrush(yellow);
    // Filled upward-pointing triangle head spanning the top ~55%.
    QPainterPath head;
    head.moveTo(px * 0.50, px * 0.12);          // apex
    head.lineTo(px * 0.88, px * 0.55);          // bottom-right
    head.lineTo(px * 0.12, px * 0.55);          // bottom-left
    head.closeSubpath();
    p.drawPath(head);
    // Short stem below the head.
    p.drawRect(QRectF(px * 0.36, px * 0.55, px * 0.28, px * 0.33));
    return QIcon(pm);
}
// Conflict flag: this mod OVERWRITES others (wins file conflicts). Green ▲.
inline QIcon greenUpTriangleIcon(int px = 16) {
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#27ae60"));
    QPainterPath t;
    t.moveTo(px * 0.50, px * 0.16);
    t.lineTo(px * 0.86, px * 0.80);
    t.lineTo(px * 0.14, px * 0.80);
    t.closeSubpath();
    p.drawPath(t);
    return QIcon(pm);
}
// Conflict flag: this mod is OVERWRITTEN by others (loses file conflicts). Red ▼.
inline QIcon redDownTriangleIcon(int px = 16) {
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#e74c3c"));
    QPainterPath t;
    t.moveTo(px * 0.50, px * 0.84);
    t.lineTo(px * 0.86, px * 0.20);
    t.lineTo(px * 0.14, px * 0.20);
    t.closeSubpath();
    p.drawPath(t);
    return QIcon(pm);
}
// Has-note flag: a small lined "page" glyph.
inline QIcon noteIcon(int px = 16) {
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen border(QColor("#95a5a6")); border.setWidthF(px * 0.07);
    p.setPen(border);
    p.setBrush(QColor("#ecf0f1"));
    QRectF r(px * 0.22, px * 0.12, px * 0.56, px * 0.76);
    p.drawRoundedRect(r, px * 0.08, px * 0.08);
    QPen line(QColor("#34495e")); line.setWidthF(px * 0.06); line.setCapStyle(Qt::RoundCap);
    p.setPen(line);
    for (int i = 0; i < 3; ++i) {
        double y = px * (0.34 + i * 0.18);
        p.drawLine(QPointF(px * 0.33, y), QPointF(px * 0.67, y));
    }
    return QIcon(pm);
}
// Lay several small icons out horizontally into a single icon (for the Flags
// column, which can only carry one DecorationRole image per cell).
inline QIcon composeIcons(const QList<QIcon>& icons, int px = 16) {
    if (icons.isEmpty()) return {};
    const int gap = 2;
    const int w = icons.size() * px + (icons.size() - 1) * gap;
    QPixmap pm(w, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    int x = 0;
    for (const QIcon& ic : icons) { ic.paint(&p, QRect(x, 0, px, px)); x += px + gap; }
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
