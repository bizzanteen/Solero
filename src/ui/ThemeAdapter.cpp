#include "ui/ThemeAdapter.h"
#include "ui/IconUtil.h"       // contrastText
#include "core/AppConfig.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

namespace solero {

namespace {

// Fusion needs the Light/Midlight/Mid/Dark/Shadow shade roles (borders, bevels,
// etched text) and sensible disabled colours. KDE and our built-in light/dark
// bases don't supply all of them, so derive them from the resolved Window colour.
void deriveShades(QPalette& pal) {
    const QColor windowBg = pal.color(QPalette::Window);
    if (!windowBg.isValid()) return;

    pal.setColor(QPalette::Light,    windowBg.lighter(150));
    pal.setColor(QPalette::Midlight, windowBg.lighter(125));
    pal.setColor(QPalette::Mid,      windowBg.darker(130));
    pal.setColor(QPalette::Dark,     windowBg.darker(160));
    pal.setColor(QPalette::Shadow,   windowBg.darker(200));
    pal.setColor(QPalette::BrightText,
                 windowBg.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::red));

    auto blend = [](const QColor& fg, const QColor& bg) {
        return QColor((fg.red() + bg.red()) / 2, (fg.green() + bg.green()) / 2,
                      (fg.blue() + bg.blue()) / 2);
    };
    pal.setColor(QPalette::Disabled, QPalette::WindowText,
                 blend(pal.color(QPalette::WindowText), windowBg));
    pal.setColor(QPalette::Disabled, QPalette::Text,
                 blend(pal.color(QPalette::Text), windowBg));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText,
                 blend(pal.color(QPalette::ButtonText), windowBg));
    pal.setColor(QPalette::Disabled, QPalette::Base,
                 blend(pal.color(QPalette::Base), windowBg));
    pal.setColor(QPalette::Disabled, QPalette::Button,
                 blend(pal.color(QPalette::Button), windowBg));

    QColor placeholder = pal.color(QPalette::Text);
    placeholder.setAlpha(128);
    pal.setColor(QPalette::PlaceholderText, placeholder);
}

QPalette darkBase() {
    QPalette p;
    p.setColor(QPalette::Window,        QColor("#353535"));
    p.setColor(QPalette::WindowText,    QColor("#f0f0f0"));
    p.setColor(QPalette::Base,          QColor("#2a2a2a"));
    p.setColor(QPalette::AlternateBase, QColor("#3a3a3a"));
    p.setColor(QPalette::Text,          QColor("#f0f0f0"));
    p.setColor(QPalette::Button,        QColor("#454545"));
    p.setColor(QPalette::ButtonText,    QColor("#f0f0f0"));
    p.setColor(QPalette::ToolTipBase,   QColor("#2a2a2a"));
    p.setColor(QPalette::ToolTipText,   QColor("#f0f0f0"));
    p.setColor(QPalette::Highlight,     QColor("#2a82da"));
    p.setColor(QPalette::HighlightedText, QColor(Qt::white));
    p.setColor(QPalette::Link,          QColor("#2a82da"));
    return p;
}

QPalette lightBase() {
    QPalette p;
    p.setColor(QPalette::Window,        QColor("#efefef"));
    p.setColor(QPalette::WindowText,    QColor("#202020"));
    p.setColor(QPalette::Base,          QColor("#ffffff"));
    p.setColor(QPalette::AlternateBase, QColor("#f6f6f6"));
    p.setColor(QPalette::Text,          QColor("#202020"));
    p.setColor(QPalette::Button,        QColor("#e6e6e6"));
    p.setColor(QPalette::ButtonText,    QColor("#202020"));
    p.setColor(QPalette::ToolTipBase,   QColor("#ffffdc"));
    p.setColor(QPalette::ToolTipText,   QColor("#202020"));
    p.setColor(QPalette::Highlight,     QColor("#308cc6"));
    p.setColor(QPalette::HighlightedText, QColor(Qt::white));
    p.setColor(QPalette::Link,          QColor("#0057ae"));
    return p;
}

// The system palette: today's behaviour - start from the app's Fusion palette and
// overlay KDE's kdeglobals colours when present. Returns the fully-derived palette.
QPalette readKdePalette(const QApplication& app) {
    QPalette pal = app.palette();

    const QString path = QDir::homePath() + "/.config/kdeglobals";
    if (!QFile::exists(path)) {
        deriveShades(pal);
        return pal;
    }
    QSettings kg(path, QSettings::IniFormat);
    // kdeglobals stores colours as unquoted "R,G,B"; QSettings parses the commas
    // into a QStringList, so re-join to recover the string.
    auto color = [](const QString& s) -> QColor {
        const auto parts = s.split(',');
        if (parts.size() < 3) return QColor();
        return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(),
                      parts[2].trimmed().toInt());
    };
    auto get = [&](const char* group, const char* key) -> QColor {
        return color(kg.value(QString(group) + "/" + key).toStringList().join(","));
    };
    auto setIf = [&](QPalette::ColorRole role, const QColor& c) {
        if (c.isValid()) pal.setColor(role, c);
    };
    setIf(QPalette::Window, get("Colors:Window", "BackgroundNormal"));
    setIf(QPalette::WindowText, get("Colors:Window", "ForegroundNormal"));
    setIf(QPalette::Base, get("Colors:View", "BackgroundNormal"));
    setIf(QPalette::Text, get("Colors:View", "ForegroundNormal"));
    setIf(QPalette::AlternateBase, get("Colors:View", "BackgroundAlternate"));
    setIf(QPalette::Link, get("Colors:View", "ForegroundLink"));
    setIf(QPalette::Button, get("Colors:Button", "BackgroundNormal"));
    setIf(QPalette::ButtonText, get("Colors:Button", "ForegroundNormal"));
    setIf(QPalette::Highlight, get("Colors:Selection", "BackgroundNormal"));
    setIf(QPalette::HighlightedText, get("Colors:Selection", "ForegroundNormal"));
    setIf(QPalette::ToolTipBase, get("Colors:Tooltip", "BackgroundNormal"));
    setIf(QPalette::ToolTipText, get("Colors:Tooltip", "ForegroundNormal"));

    deriveShades(pal); // shade roles + disabled + placeholder from the resolved window
    return pal;
}

} // namespace

QPalette ThemeAdapter::buildPalette(const QString& mode, const QColor& accent,
                                    const QPalette& systemBase) {
    QPalette pal;
    if (mode == QLatin1String("light")) { pal = lightBase(); deriveShades(pal); }
    else if (mode == QLatin1String("dark")) { pal = darkBase(); deriveShades(pal); }
    else pal = systemBase; // already fully derived by readKdePalette()

    if (accent.isValid()) {
        pal.setColor(QPalette::Highlight, accent);
        pal.setColor(QPalette::HighlightedText, contrastText(accent));
        pal.setColor(QPalette::Link, accent);
    }
    return pal;
}

void ThemeAdapter::apply(QApplication& app) {
    // Fusion is a consistent base style that fully honours the QPalette we build.
    app.setStyle("Fusion");

    AppConfig& cfg = AppConfig::instance();
    const QPalette systemBase = readKdePalette(app);
    const QColor accent(cfg.accentColor()); // invalid if empty / malformed
    app.setPalette(buildPalette(cfg.themeMode(), accent, systemBase));

    // Font override (family / point size). Empty family + size 0 => Qt default.
    const QString fam = cfg.fontFamily();
    const int sz = cfg.fontSize();
    if (!fam.isEmpty() || sz > 0) {
        QFont f = app.font();
        if (!fam.isEmpty()) f.setFamily(fam);
        if (sz > 0) f.setPointSize(sz);
        app.setFont(f);
    }
}

} // namespace solero
