#pragma once
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QColor>
#include <QSvgRenderer>
namespace solero {
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
