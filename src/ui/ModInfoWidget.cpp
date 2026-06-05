#include "ModInfoWidget.h"
#include "core/Profile.h"
#include "core/ModList.h"
#include "core/AppConfig.h"
#include "tools/ToolCatalog.h"

#include <QDir>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QTextBrowser>
#include <QFont>
#include <QPixmap>
#include <QFileInfo>
#include <QRegularExpression>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

namespace solero {

ModInfoWidget::ModInfoWidget(QWidget* parent) : QWidget(parent) {
    m_nam = new QNetworkAccessManager(this);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(12);

    m_image = new QLabel(this);
    m_image->setFixedSize(200, 130);
    m_image->setScaledContents(false);
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setFrameShape(QFrame::StyledPanel);
    root->addWidget(m_image, 0, Qt::AlignTop);

    auto* right = new QVBoxLayout();
    right->setSpacing(4);

    m_name = new QLabel(this);
    QFont nf = m_name->font();
    nf.setBold(true);
    nf.setPointSizeF(nf.pointSizeF() + 2.0);
    m_name->setFont(nf);
    m_name->setWordWrap(true);
    right->addWidget(m_name);

    m_meta = new QLabel(this);
    m_meta->setWordWrap(true);
    right->addWidget(m_meta);

    m_desc = new QTextBrowser(this);
    m_desc->setReadOnly(true);
    m_desc->setOpenExternalLinks(true);
    right->addWidget(m_desc, 1);

    root->addLayout(right, 1);

    // When the async Nexus details fetch finishes, apply only if still current.
    connect(&m_detailsWatcher, &QFutureWatcher<NexusApi::ModDetails>::finished,
            this, [this]() {
        if (m_detailsWatcher.isCanceled()) return;
        const NexusApi::ModDetails d = m_detailsWatcher.result();
        if (!d.ok || d.modId.isEmpty()) return;
        // Cache regardless of current selection so re-selecting is instant.
        m_detailCache.insert(d.modId, d);
        // Apply only if this result is for the mod still selected.
        if (d.modId == m_currentNexusId) applyDetails(d);
    });

    clear();
}

void ModInfoWidget::clear() {
    m_currentId.clear();
    m_currentNexusId.clear();
    m_expectedImageKey.clear();
    m_localName.clear();
    m_localMeta.clear();
    if (m_name) m_name->setText("Select a mod to see info");
    if (m_meta) m_meta->clear();
    if (m_desc) m_desc->clear();
    if (m_image) m_image->setPixmap(QPixmap());
}

// Convert common Nexus BBCode to safe HTML for QTextBrowser. The raw text is
// HTML-escaped first so author content can't inject markup; then a small set of
// BBCode tags is mapped to HTML. Unrecognised tags are stripped (inner text
// kept) and remaining newlines become <br>.
static QString bbcodeToHtml(const QString& in) {
    QString s = in.toHtmlEscaped();
    const auto ci = QRegularExpression::CaseInsensitiveOption;

    // Inline formatting.
    s.replace(QRegularExpression("\\[b\\](.*?)\\[/b\\]", ci | QRegularExpression::DotMatchesEverythingOption), "<b>\\1</b>");
    s.replace(QRegularExpression("\\[i\\](.*?)\\[/i\\]", ci | QRegularExpression::DotMatchesEverythingOption), "<i>\\1</i>");
    s.replace(QRegularExpression("\\[u\\](.*?)\\[/u\\]", ci | QRegularExpression::DotMatchesEverythingOption), "<u>\\1</u>");

    // Links: [url=HREF]text[/url] and [url]HREF[/url].
    s.replace(QRegularExpression("\\[url=([^\\]]+)\\](.*?)\\[/url\\]", ci | QRegularExpression::DotMatchesEverythingOption),
              "<a href=\"\\1\">\\2</a>");
    s.replace(QRegularExpression("\\[url\\](.*?)\\[/url\\]", ci | QRegularExpression::DotMatchesEverythingOption),
              "<a href=\"\\1\">\\1</a>");

    // Drop images entirely (tag + URL).
    s.replace(QRegularExpression("\\[img\\].*?\\[/img\\]", ci | QRegularExpression::DotMatchesEverythingOption), "");

    // Lists: turn [*] items into <li>, wrap a [list]…[/list] block in <ul>.
    s.replace(QRegularExpression("\\[\\*\\]\\s*", ci), "<li>");
    s.replace(QRegularExpression("\\[list[^\\]]*\\]", ci), "<ul>");
    s.replace(QRegularExpression("\\[/list\\]", ci), "</ul>");

    // Strip remaining container/decoration tags, keeping inner text.
    s.replace(QRegularExpression("\\[/?(size|color|quote)[^\\]]*\\]", ci), "");

    // Any leftover BBCode-style tags.
    s.replace(QRegularExpression("\\[/?[^\\]]*\\]"), "");

    // Newlines -> <br>.
    s.replace(QRegularExpression("\\r?\\n"), "<br>");
    return s.trimmed();
}

void ModInfoWidget::showMod(Profile* profile, const QString& id) {
    if (!profile || id.isEmpty() || id == "__overwrite__" || id == "__separator__") {
        clear();
        return;
    }

    const ModEntry* entry = profile->modList().findById(id);
    if (!entry) { clear(); return; }

    m_currentId = id;
    m_currentNexusId = entry->nexusModId;
    m_expectedImageKey.clear();

    const QString staging = AppConfig::instance().stagingDir() + "/" + id;

    // Output mods: match against the tool catalog and show the tool image
    if (entry->isOutputMod) {
        const ToolPreset* match = nullptr;
        for (const ToolPreset& p : ToolCatalog::presets()) {
            if (p.outputModName.compare(entry->name, Qt::CaseInsensitive) == 0) {
                match = &p; break;
            }
            for (const auto& a : p.extraActions) {
                if (a.outputModName.compare(entry->name, Qt::CaseInsensitive) == 0) {
                    match = &p; break;
                }
            }
            if (match) break;
        }

        m_localName = entry->name;
        m_name->setText(entry->name);
        m_image->setPixmap(QPixmap());

        if (match) {
            m_localMeta = "Generated by " + match->name;
            m_meta->setText(m_localMeta);
            m_desc->setPlainText(
                QString("Output mod for %1.\n\nStaging: %2").arg(match->name, staging));

            // Thumbnail: prefer the tool's Nexus image (disk-cached, keyed by the
            // TOOL's nexusModId); fall back to the bundled icon resource.
            const bool canFetchNexus = match->source == ToolSource::Nexus
                                       && !match->nexusModId.isEmpty()
                                       && NexusApi::keyAvailable();
            if (canFetchNexus) {
                m_expectedImageKey = match->nexusModId;
                loadThumbnailFor(match->nexusModId);
            } else {
                QPixmap icon(match->iconResource);
                if (!icon.isNull()) applyImage(icon);
            }
        } else {
            // Output mod from a tool not in the catalog: local info only.
            m_localMeta = entry->enabled ? "enabled" : "disabled";
            m_meta->setText(m_localMeta);
            m_desc->setPlainText(
                QString("Output mod (generating tool not in catalog).\n\nStaging: %1").arg(staging));
        }
        return;
    }

    // Local info, synchronous (instant, no network)
    m_localName = entry->name;
    m_name->setText(entry->name);

    QStringList metaBits;
    if (!entry->version.isEmpty()) metaBits << ("Version " + entry->version);
    metaBits << (entry->enabled ? "enabled" : "disabled");
    m_localMeta = metaBits.join("  \xe2\x80\xa2  ");
    m_meta->setText(m_localMeta);

    QStringList facts;
    facts << ("Staging: " + staging);
    if (!entry->sourceArchive.isEmpty())
        facts << ("Source: " + QFileInfo(entry->sourceArchive).fileName());
    if (!entry->nexusModId.isEmpty())
        facts << ("Nexus mod ID: " + entry->nexusModId);
    m_desc->setPlainText(facts.join("\n"));

    m_image->setPixmap(QPixmap());

    // Thumbnail: instant from memory/disk, else fetch (shared helper)
    const QString nexusId = entry->nexusModId;
    if (!nexusId.isEmpty()) {
        m_expectedImageKey = nexusId;
        loadThumbnailFor(nexusId);
    }

    // Rich Nexus details, async + cached (text refreshes live)
    if (nexusId.isEmpty() || !NexusApi::keyAvailable())
        return;

    if (m_detailCache.contains(nexusId)) {
        applyDetails(m_detailCache.value(nexusId));
        return;
    }

    // Miss -> fetch details off the UI thread.
    m_detailsWatcher.setFuture(QtConcurrent::run([nexusId]() {
        return NexusApi::modDetails(nexusId);
    }));
}

QString ModInfoWidget::thumbnailPath(const QString& nexusModId) {
    const QString dir = AppConfig::dataRoot() + "/thumbnails";
    QDir().mkpath(dir);
    return dir + "/" + nexusModId + ".png";
}

void ModInfoWidget::loadThumbnailFor(const QString& nexusModId, const QString& pictureUrlIfKnown) {
    if (nexusModId.isEmpty()) return;

    // 1) In-memory cache.
    if (m_imageCache.contains(nexusModId)) {
        if (nexusModId == m_expectedImageKey) applyImage(m_imageCache.value(nexusModId));
        return;
    }

    // 2) On-disk PNG (offline / survives restart).
    const QString path = thumbnailPath(nexusModId);
    if (QFileInfo::exists(path)) {
        QPixmap pm;
        if (pm.load(path)) {
            m_imageCache.insert(nexusModId, pm);
            if (nexusModId == m_expectedImageKey) applyImage(pm);
            return;
        }
    }

    // 3) Network. Need a picture URL; resolve via modDetails if not supplied.
    if (!NexusApi::keyAvailable()) return;
    if (!pictureUrlIfKnown.isEmpty()) {
        fetchImage(nexusModId, pictureUrlIfKnown);
        return;
    }
    if (m_detailCache.contains(nexusModId)) {
        const QString url = m_detailCache.value(nexusModId).pictureUrl;
        if (!url.isEmpty()) fetchImage(nexusModId, url);
        return;
    }
    QPointer<ModInfoWidget> self(this);
    auto* w = new QFutureWatcher<NexusApi::ModDetails>(this);
    connect(w, &QFutureWatcher<NexusApi::ModDetails>::finished, this, [self, w, nexusModId]() {
        w->deleteLater();
        if (!self) return;
        const NexusApi::ModDetails d = w->result();
        if (!d.ok || d.modId.isEmpty()) return;
        self->m_detailCache.insert(d.modId, d);
        if (!d.pictureUrl.isEmpty()) self->fetchImage(d.modId, d.pictureUrl);
    });
    w->setFuture(QtConcurrent::run([nexusModId]() {
        return NexusApi::modDetails(nexusModId);
    }));
}

// Precondition: caller has confirmed d is for the currently selected mod
// (d.modId == m_currentNexusId).
void ModInfoWidget::applyDetails(const NexusApi::ModDetails& d) {
    if (!d.name.isEmpty()) m_name->setText(d.name);

    QStringList bits;
    if (!d.author.isEmpty()) bits << d.author;
    // Prefer the Nexus version; fall back to whatever local meta already showed.
    if (!d.version.isEmpty())
        bits << ("Version " + d.version);
    if (d.endorsements > 0)
        bits << QString("%1 endorsements").arg(d.endorsements);
    if (!bits.isEmpty())
        m_meta->setText(bits.join("  \xe2\x80\xa2  "));

    const QString body = d.summary.isEmpty() ? d.description : d.summary;
    if (!body.isEmpty())
        m_desc->setHtml(bbcodeToHtml(body));

    // Image goes through the shared disk-cached loader (it may already be on disk).
    loadThumbnailFor(d.modId, d.pictureUrl);
}

void ModInfoWidget::fetchImage(const QString& nexusModId, const QString& url) {
    if (m_imageCache.contains(nexusModId)) {
        if (nexusModId == m_expectedImageKey) applyImage(m_imageCache.value(nexusModId));
        return;
    }
    QNetworkRequest req{QUrl(url)};
    QNetworkReply* reply = m_nam->get(req);
    QPointer<ModInfoWidget> self(this);
    const QString fetchedFor = nexusModId;
    connect(reply, &QNetworkReply::finished, this, [self, reply, fetchedFor]() {
        reply->deleteLater();
        if (!self) return;
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        // Nexus bytes are often WebP; decoding then saving as PNG normalizes it.
        if (!pm.loadFromData(reply->readAll())) return;
        self->m_imageCache.insert(fetchedFor, pm);
        pm.save(ModInfoWidget::thumbnailPath(fetchedFor), "PNG");
        if (fetchedFor == self->m_expectedImageKey)
            self->applyImage(pm);
    });
}

void ModInfoWidget::applyImage(const QPixmap& pm) {
    if (pm.isNull()) return;
    m_image->setPixmap(pm.scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace solero
