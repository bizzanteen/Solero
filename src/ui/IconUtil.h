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
#include <QFont>
#include <QGuiApplication>
#include <QSvgRenderer>
namespace solero {
// Canonical square pixel size for every Flags-column status icon. Keeping it in
// one place lets every helper and composeIcons() lay out at the same size, so a
// row with 1, 2 or 3 flags looks consistent. (The view must then render the
// composed strip at its natural size - see FlagsDelegate in ModListView.cpp.)
inline constexpr int kFlagIconPx = 16;

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
// Shared "road-sign" severity glyph: an equilateral, point-up triangle with
// softly rounded corners and a bold exclamation mark painted (not a font glyph,
// which would misalign against the sloped sides). Colour signals severity -
// warm yellow = warning, red = error, dim grey = neutral/no problems. Every
// health surface (toolbar indicator, Problems panel, Flags column) shares this
// one shape so a warning always reads as a warning.
inline QIcon signTriangleIcon(int size, const QColor& fill,
                              const QColor& border, const QColor& mark) {
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    const qreal px     = size;
    const qreal margin = px * 0.10;
    const qreal side   = px - 2 * margin;
    const qreal height = side * 0.86602540;      // equilateral: √3/2 · side
    const qreal topY   = (px - height) / 2.0;    // vertically centred
    const qreal cx     = px / 2.0;

    // Triangle body. A RoundJoin pen rounds the three corners; the same pen
    // draws the 1px darker border in a single stroke+fill pass.
    QPainterPath tri;
    tri.moveTo(cx, topY);
    tri.lineTo(px - margin, topY + height);
    tri.lineTo(margin,      topY + height);
    tri.closeSubpath();
    QPen edge(border);
    edge.setWidthF(qMax<qreal>(1.0, px * 0.075));
    edge.setJoinStyle(Qt::RoundJoin);
    p.setPen(edge);
    p.setBrush(fill);
    p.drawPath(tri);

    // Exclamation mark: a rounded-cap vertical bar plus a dot, together spanning
    // ~55% of the triangle height and centred on the lower (wider) body.
    const qreal markH      = height * 0.55;
    const qreal markCenter = topY + height * 0.66;   // near the centroid
    const qreal markTop    = markCenter - markH / 2.0;
    const qreal barLen     = markH * 0.62;
    const qreal dotR       = px * 0.055;
    QPen bar(mark);
    bar.setWidthF(px * 0.105);
    bar.setCapStyle(Qt::RoundCap);
    p.setPen(bar);
    p.drawLine(QPointF(cx, markTop), QPointF(cx, markTop + barLen));
    p.setPen(Qt::NoPen);
    p.setBrush(mark);
    p.drawEllipse(QPointF(cx, markTop + markH - dotR), dotR, dotR);

    p.end();
    return QIcon(pm);
}
inline QIcon warnSignIcon(int size = 20) {
    return signTriangleIcon(size, QColor("#F2C230"), QColor("#B8901F"), QColor(Qt::black));
}
inline QIcon errorSignIcon(int size = 20) {
    return signTriangleIcon(size, QColor("#D64550"), QColor("#A33038"), QColor(Qt::white));
}
inline QIcon neutralSignIcon(int size = 20) {
    return signTriangleIcon(size, QColor("#5a5f66"), QColor("#42464c"), QColor("#3a3d42"));
}

// Downloads "status dot" icons
// Small DPR-aware circles for the Downloads list Status column. A filled circle
// carries a white glyph (check / down-arrow / en-dash); "not installed" is a
// quiet hollow grey ring with no glyph. Same painted-QPixmap approach as the
// sign icons so the whole app's status family stays visually consistent.

// Shared setup: a transparent DPR-scaled pixmap + an antialiased painter on it.
// Returns the pixmap by reference; the caller keeps the QPainter alive locally.
inline QPixmap makeStatusPixmap(int size) {
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pm(qRound(size * dpr), qRound(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    return pm;
}

// Installed: green (#4CAF50, matching the former green status text) filled
// circle with a white check.
inline QIcon installedStatusIcon(int size = 14) {
    QPixmap pm = makeStatusPixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal px = size, m = px * 0.08;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#4CAF50"));
    p.drawEllipse(QRectF(m, m, px - 2 * m, px - 2 * m));
    QPen check(Qt::white);
    check.setWidthF(px * 0.13);
    check.setCapStyle(Qt::RoundCap);
    check.setJoinStyle(Qt::RoundJoin);
    p.setPen(check);
    p.setBrush(Qt::NoBrush);
    QPainterPath tick;
    tick.moveTo(px * 0.28, px * 0.52);
    tick.lineTo(px * 0.44, px * 0.68);
    tick.lineTo(px * 0.72, px * 0.33);
    p.drawPath(tick);
    return QIcon(pm);
}

// Not installed: a quiet hollow grey ring, no glyph.
inline QIcon notInstalledStatusIcon(int size = 14) {
    QPixmap pm = makeStatusPixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal px = size, m = px * 0.16;
    QPen ring(QColor("#7a7f87"));
    ring.setWidthF(px * 0.11);
    p.setPen(ring);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(m, m, px - 2 * m, px - 2 * m));
    return QIcon(pm);
}

// Downloading: blue filled circle with a white down-arrow.
inline QIcon downloadingStatusIcon(int size = 14) {
    QPixmap pm = makeStatusPixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal px = size, m = px * 0.08;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2196F3"));
    p.drawEllipse(QRectF(m, m, px - 2 * m, px - 2 * m));
    QPen arrow(Qt::white);
    arrow.setWidthF(px * 0.12);
    arrow.setCapStyle(Qt::RoundCap);
    arrow.setJoinStyle(Qt::RoundJoin);
    p.setPen(arrow);
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(px * 0.50, px * 0.28), QPointF(px * 0.50, px * 0.64));
    QPainterPath head;
    head.moveTo(px * 0.32, px * 0.50);
    head.lineTo(px * 0.50, px * 0.68);
    head.lineTo(px * 0.68, px * 0.50);
    p.drawPath(head);
    return QIcon(pm);
}

// Cancelled / terminal-neutral: grey filled circle with a white en-dash.
inline QIcon neutralStatusIcon(int size = 14) {
    QPixmap pm = makeStatusPixmap(size);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal px = size, m = px * 0.08;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#6b7078"));
    p.drawEllipse(QRectF(m, m, px - 2 * m, px - 2 * m));
    QPen dash(Qt::white);
    dash.setWidthF(px * 0.13);
    dash.setCapStyle(Qt::RoundCap);
    p.setPen(dash);
    p.drawLine(QPointF(px * 0.30, px * 0.50), QPointF(px * 0.70, px * 0.50));
    return QIcon(pm);
}
inline QIcon yellowUpArrowIcon(int px = kFlagIconPx) {
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
inline QIcon greenUpTriangleIcon(int px = kFlagIconPx) {
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
inline QIcon redDownTriangleIcon(int px = kFlagIconPx) {
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
inline QIcon noteIcon(int px = kFlagIconPx) {
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
// FOMOD badge: a small rounded square carrying a white "F". The fill colour
// signals how the choices were recovered - green when reconstructed/manual,
// amber when the installer is flag-driven and needs a re-run, slate otherwise.
inline QIcon fomodIcon(const QString& status, int px = kFlagIconPx) {
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor fill("#2980b9"); // detected (no choice info)
    if (status == "reconstructed" || status == "manual") fill = QColor("#27ae60");
    else if (status == "needs-rerun")                    fill = QColor("#e67e22");
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(px * 0.08, px * 0.08, px * 0.84, px * 0.84),
                      px * 0.18, px * 0.18);
    QFont f;
    f.setPixelSize(int(px * 0.66));
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, 0, px, px), Qt::AlignCenter, "F");
    return QIcon(pm);
}
// Output-mod badge: an outbox tray (a shelf/box with an up-arrow leaving it),
// muted blue to match the output-mod text colour. Marks a tool's capture mod.
inline QIcon outputModIcon(int px = kFlagIconPx) {
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor c("#7f9cc4");
    QPen stroke(c); stroke.setWidthF(px * 0.09);
    stroke.setJoinStyle(Qt::RoundJoin); stroke.setCapStyle(Qt::RoundCap);
    p.setPen(stroke);
    p.setBrush(Qt::NoBrush);
    // Tray: an open-topped box in the lower half.
    const double left = px * 0.20, right = px * 0.80, top = px * 0.56, bot = px * 0.82;
    p.drawLine(QPointF(left, top), QPointF(left, bot));
    p.drawLine(QPointF(right, top), QPointF(right, bot));
    p.drawLine(QPointF(left, bot), QPointF(right, bot));
    // Up-arrow rising out of the tray.
    const double cx = px * 0.50;
    p.drawLine(QPointF(cx, px * 0.14), QPointF(cx, px * 0.50));
    p.drawLine(QPointF(cx, px * 0.14), QPointF(px * 0.34, px * 0.30));
    p.drawLine(QPointF(cx, px * 0.14), QPointF(px * 0.66, px * 0.30));
    return QIcon(pm);
}
// Lay several small icons out horizontally into a single icon (for the Flags
// column, which can only carry one DecorationRole image per cell).
inline QIcon composeIcons(const QList<QIcon>& icons, int px = kFlagIconPx) {
    if (icons.isEmpty()) return {};
    const int gap = qMax(2, px / 4); // even, size-proportional spacing
    const int w = icons.size() * px + (icons.size() - 1) * gap;
    QPixmap pm(w, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    int x = 0;
    // Each icon is painted, centred, into an identical px×px cell so every flag
    // is the same size regardless of how many share the strip.
    for (const QIcon& ic : icons) {
        ic.paint(&p, QRect(x, 0, px, px), Qt::AlignCenter);
        x += px + gap;
    }
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
