#pragma once
#include <QPalette>
#include <QString>
#include <QColor>
class QApplication;
namespace solero {
class ThemeAdapter {
public:
    // Apply the user's theme (mode / accent / font from AppConfig) to the app.
    static void apply(QApplication& app);

    // Pure: build the final palette for a theme mode ("system" | "light" | "dark")
    // plus an optional accent, given the system-derived base palette (used when
    // mode == "system"). No AppConfig / filesystem access - unit-testable.
    static QPalette buildPalette(const QString& mode, const QColor& accent,
                                 const QPalette& systemBase);

    // GNOME "System" following (pure, testable). gnomeSchemeIsDark maps the
    // org.gnome.desktop.interface color-scheme value ("prefer-dark" etc., possibly
    // single-quoted by gsettings) to dark/light. gnomeAccentColor maps a GNOME 47+
    // accent-color name ("blue", "teal", …) to its colour, or an invalid QColor.
    static bool gnomeSchemeIsDark(const QString& colorScheme);
    static QColor gnomeAccentColor(const QString& accentName);
};
}
