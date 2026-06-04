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
}
