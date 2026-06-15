#pragma once
#include <QString>
#include <QStringList>
class QJsonArray;

namespace solero {

// Pure helpers over a Nexus download_link.json mirror array. Each element is a
// JSON object with "short_name" / "name" / "URI". pickMirror returns the URI of
// the entry whose short_name matches `preferred` (case-insensitive); it falls
// back to the first entry's URI when `preferred` is empty or not found. Returns
// an empty string for an empty array.
QString pickMirror(const QJsonArray& mirrors, const QString& preferred);

// All mirror short_names in array order - for populating a preference combo.
QStringList mirrorServerNames(const QJsonArray& mirrors);

} // namespace solero
