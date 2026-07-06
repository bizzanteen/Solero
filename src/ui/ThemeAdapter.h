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
};
}
