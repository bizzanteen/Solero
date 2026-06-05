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

    QLabel*        m_image = nullptr;
    QLabel*        m_name  = nullptr;
    QLabel*        m_meta  = nullptr;
    QTextBrowser*  m_desc  = nullptr;

    QString m_currentId;       // mod entry id currently shown (guards stale async results)
    QString m_currentNexusId;  // nexusModId of the current entry (async results key off this)
    QString m_localName, m_localMeta; // local fallback while/if no Nexus data

    QNetworkAccessManager*               m_nam = nullptr;
    QFutureWatcher<NexusApi::ModDetails> m_detailsWatcher;

    QHash<QString, NexusApi::ModDetails> m_detailCache; // keyed by nexusModId
    QHash<QString, QPixmap>              m_imageCache;   // keyed by nexusModId
};

} // namespace solero
