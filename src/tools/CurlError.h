#pragma once
#include <QString>

namespace solero {

// Returns the last non-empty line from curl stderr that looks user-readable
// (not a raw path, not a debug marker such as "exit code" or "errno").
// Returns empty string when stderr contains nothing useful.
//
// Usage: build the final message in the caller's context, e.g.
//   const QString hint = curlStderrHint(stderrText);
//   const QString msg = "Could not reach Nexus - check your internet connection."
//                     + (hint.isEmpty() ? QString() : " (" + hint + ")");
inline QString curlStderrHint(const QString& stderrText) {
    QString last;
    for (const QString& l : stderrText.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
        if (!l.trimmed().isEmpty()) last = l.trimmed();
    if (!last.isEmpty()
        && !last.startsWith(QLatin1Char('/'))
        && !last.contains(QLatin1String("exit code"), Qt::CaseInsensitive)
        && !last.contains(QLatin1String("errno"), Qt::CaseInsensitive))
        return last;
    return {};
}

} // namespace solero
