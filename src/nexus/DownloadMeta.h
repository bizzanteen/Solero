#pragma once
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QRegularExpression>

namespace solero {

// Nexus identity recovered for a file the website downloaded itself, rather than
// one Solero fetched through the API. Any field may be empty.
struct NexusDownloadMeta {
    QString modId, fileId, version, name;
};

// Nexus names its own downloads "<name>-<modId>-<version with dashes>-<unix ts>.<ext>",
// e.g. "eFPS - Official Patch Hub-54998-1-7a-1648632543.zip". The name half can
// itself contain dashes, so anchor on the trailing timestamp and work backwards.
inline NexusDownloadMeta nexusMetaFromFileName(const QString& fileName) {
    NexusDownloadMeta m;
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? fileName.left(dot) : fileName;
    static const QRegularExpression re(
        QStringLiteral(R"(^(.*?)-(\d+)-(.+)-(\d{9,11})$)"));
    const auto match = re.match(stem);
    if (!match.hasMatch()) { m.name = stem; return m; }
    m.name    = match.captured(1);
    m.modId   = match.captured(2);
    m.version = match.captured(3);
    m.version.replace(QLatin1Char('-'), QLatin1Char('.'));
    return m;
}

// A mod page carries the ids directly: /<game>/mods/<modId>?tab=files&file_id=<fileId>.
// More trustworthy than the filename, so it wins where both are present.
inline NexusDownloadMeta nexusMetaFromPageUrl(const QUrl& pageUrl) {
    NexusDownloadMeta m;
    if (!pageUrl.host().endsWith(QLatin1String("nexusmods.com"), Qt::CaseInsensitive))
        return m;
    static const QRegularExpression re(QStringLiteral(R"(/mods/(\d+))"));
    const auto match = re.match(pageUrl.path());
    if (match.hasMatch()) m.modId = match.captured(1);
    const QString fid = QUrlQuery(pageUrl).queryItemValue(QStringLiteral("file_id"));
    if (!fid.isEmpty()) m.fileId = fid;
    return m;
}

// Page ids win; the filename fills the gaps.
inline NexusDownloadMeta mergeNexusMeta(const NexusDownloadMeta& page,
                                        const NexusDownloadMeta& file) {
    NexusDownloadMeta m = page;
    if (m.modId.isEmpty())   m.modId = file.modId;
    if (m.fileId.isEmpty())  m.fileId = file.fileId;
    if (m.version.isEmpty()) m.version = file.version;
    if (m.name.isEmpty())    m.name = file.name;
    return m;
}

} // namespace solero
