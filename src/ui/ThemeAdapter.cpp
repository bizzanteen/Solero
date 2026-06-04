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

    app.setPalette(pal);
}

} // namespace solero
