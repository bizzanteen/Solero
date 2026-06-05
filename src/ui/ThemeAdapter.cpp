#include "ui/ThemeAdapter.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

namespace solero {

void ThemeAdapter::apply(QApplication& app) {
    // Fusion is a consistent base style that fully honors the QPalette we build.
    app.setStyle("Fusion");

    const QString path = QDir::homePath() + "/.config/kdeglobals";
    if (!QFile::exists(path))
        return;

    QSettings kg(path, QSettings::IniFormat);

    auto color = [](const QString& s) -> QColor {
        const auto parts = s.split(',');
        if (parts.size() < 3)
            return QColor();
        return QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(),
                      parts[2].trimmed().toInt());
    };

    // QSettings INI groups use '/' separators; a key in [Colors:Window] named
    // BackgroundNormal is read as "Colors:Window/BackgroundNormal".
    // kdeglobals stores colors as comma-separated "R,G,B"; QSettings IniFormat
    // parses unquoted commas into a QStringList, so toString() is empty and we
    // must re-join the list to recover the "R,G,B" string.
    auto get = [&](const char* group, const char* key) -> QColor {
        return color(kg.value(QString(group) + "/" + key).toStringList().join(","));
    };

    QPalette pal = app.palette();

    auto setIf = [&](QPalette::ColorRole role, const QColor& c) {
        if (c.isValid())
            pal.setColor(role, c);
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

    // PlaceholderText: semi-transparent version of the text color.
    const QColor viewText = get("Colors:View", "ForegroundNormal");
    if (viewText.isValid()) {
        QColor placeholder = viewText;
        placeholder.setAlpha(128);
        pal.setColor(QPalette::PlaceholderText, placeholder);
    }

    const QColor windowBg = get("Colors:Window", "BackgroundNormal");

    // Disabled foregrounds: prefer the explicit ForegroundInactive entry, else
    // blend the normal foreground 50% toward the window background.
    auto blend = [](const QColor& fg, const QColor& bg) -> QColor {
        if (!fg.isValid())
            return QColor();
        if (!bg.isValid())
            return fg;
        return QColor((fg.red() + bg.red()) / 2, (fg.green() + bg.green()) / 2,
                      (fg.blue() + bg.blue()) / 2);
    };

    auto setDisabled = [&](QPalette::ColorRole role, const char* group,
                           const char* normalKey) {
        QColor c = get(group, "ForegroundInactive");
        if (!c.isValid())
            c = blend(get(group, normalKey), windowBg);
        if (c.isValid())
            pal.setColor(QPalette::Disabled, role, c);
    };

    setDisabled(QPalette::WindowText, "Colors:Window", "ForegroundNormal");
    setDisabled(QPalette::Text, "Colors:View", "ForegroundNormal");
    setDisabled(QPalette::ButtonText, "Colors:Button", "ForegroundNormal");

    // Fusion uses the Light/Midlight/Mid/Dark/Shadow shade roles to draw
    // borders, bevels and etched text. KDE doesn't supply these, so derive them
    // from the resolved Window/Button colors - otherwise light/non-default
    // themes get mismatched borders and unreadable etched text.
    if (windowBg.isValid()) {
        pal.setColor(QPalette::Light,    windowBg.lighter(150));
        pal.setColor(QPalette::Midlight, windowBg.lighter(125));
        pal.setColor(QPalette::Mid,      windowBg.darker(130));
        pal.setColor(QPalette::Dark,     windowBg.darker(160));
        pal.setColor(QPalette::Shadow,   windowBg.darker(200));
        // BrightText is the contrasting bright color (Fusion default: white,
        // falling back to red on already-light themes).
        pal.setColor(QPalette::BrightText,
                     windowBg.lightness() < 128 ? QColor(Qt::white)
                                                : QColor(Qt::red));
    }

    // Dim the Base/Button backgrounds for disabled widgets so they read as
    // inactive (in addition to the disabled text roles above).
    const QColor base = get("Colors:View", "BackgroundNormal");
    if (base.isValid())
        pal.setColor(QPalette::Disabled, QPalette::Base, blend(base, windowBg));
    const QColor button = get("Colors:Button", "BackgroundNormal");
    if (button.isValid())
        pal.setColor(QPalette::Disabled, QPalette::Button, blend(button, windowBg));

    app.setPalette(pal);
}

} // namespace solero
