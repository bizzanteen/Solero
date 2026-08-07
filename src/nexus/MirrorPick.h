#pragma once
#include <QString>
#include <QStringList>
class QJsonArray;

namespace solero {

// Helpers over download_link.json's mirror array (short_name / name / URI).
// pickMirror takes the entry matching `preferred`, else the first one.
QString pickMirror(const QJsonArray& mirrors, const QString& preferred);

// All mirror short_names in array order - for populating a preference combo.
QStringList mirrorServerNames(const QJsonArray& mirrors);

} // namespace solero
