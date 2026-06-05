#pragma once
#include <QWidget>
#include <QHash>
#include <QPixmap>
#include <QFutureWatcher>
#include "nexus/NexusApi.h"

class QLabel;
class QTextBrowser;
class QNetworkAccessManager;

namespace solero {
class Profile;

// Bottom "Mod Info" tab. Shows the selected mod's name / version / author /
// description / thumbnail. Local facts populate synchronously; rich Nexus data
// (author, summary, image) is fetched asynchronously and cached per nexusModId.
class ModInfoWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModInfoWidget(QWidget* parent = nullptr);

    // Populate for the given mod id (looked up in profile->modList()). Pseudo ids
    // ("__overwrite__"/"__separator__"), empty id, or null profile clear the panel.
    void showMod(Profile* profile, const QString& id);
    void clear();

private:
    void applyDetails(const NexusApi::ModDetails& d);
    void applyImage(const QPixmap& pm);
    void fetchImage(const QString& nexusModId, const QString& url);

    // Disk-cached thumbnail loader shared by regular mods and output-mod tool
    // images. Tries in-memory cache, then on-disk PNG, else fetches over the
    // network (using pictureUrlIfKnown, or first resolving modDetails). The
    // resulting image is applied only if nexusModId still matches the image key
    // currently expected for the selection (m_expectedImageKey).
    void loadThumbnailFor(const QString& nexusModId, const QString& pictureUrlIfKnown = {});
    // Path to the on-disk PNG thumbnail for a given nexusModId (mkpath on call).
    static QString thumbnailPath(const QString& nexusModId);

    QLabel*        m_image = nullptr;
    QLabel*        m_name  = nullptr;
    QLabel*        m_meta  = nullptr;
    QTextBrowser*  m_desc  = nullptr;

    QString m_currentId;       // mod entry id currently shown (guards stale async results)
    QString m_currentNexusId;  // nexusModId of the current entry (details results key off this)
    QString m_expectedImageKey; // nexusModId the currently-expected image is keyed by
                                // (the mod's own id for regular mods, the TOOL's id for output mods)
    QString m_localName, m_localMeta; // local fallback while/if no Nexus data

    QNetworkAccessManager*               m_nam = nullptr;
    QFutureWatcher<NexusApi::ModDetails> m_detailsWatcher;

    QHash<QString, NexusApi::ModDetails> m_detailCache; // keyed by nexusModId
    QHash<QString, QPixmap>              m_imageCache;   // keyed by nexusModId
};

} // namespace solero
