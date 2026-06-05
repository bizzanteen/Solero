#include "ModInfoWidget.h"
#include "core/Profile.h"
#include "core/ModList.h"
#include "core/AppConfig.h"

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
    m_localName.clear();
    m_localMeta.clear();
    if (m_name) m_name->setText("Select a mod to see info");
    if (m_meta) m_meta->clear();
    if (m_desc) m_desc->clear();
    if (m_image) m_image->setPixmap(QPixmap());
}

static QString stripBBCode(const QString& in) {
    QString s = in;
    s.replace(QRegularExpression("\\[/?[^\\]]*\\]"), "");
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

    // Local info, synchronous (instant, no network)
    m_localName = entry->name;
    m_name->setText(entry->name);

    QStringList metaBits;
    if (!entry->version.isEmpty()) metaBits << ("Version " + entry->version);
    metaBits << (entry->enabled ? "enabled" : "disabled");
    m_localMeta = metaBits.join("  \xe2\x80\xa2  ");
    m_meta->setText(m_localMeta);

    QStringList facts;
    facts << ("Staging: " + AppConfig::instance().stagingDir() + "/" + id);
    if (!entry->sourceArchive.isEmpty())
        facts << ("Source: " + QFileInfo(entry->sourceArchive).fileName());
    if (!entry->nexusModId.isEmpty())
        facts << ("Nexus mod ID: " + entry->nexusModId);
    m_desc->setPlainText(facts.join("\n"));

    m_image->setPixmap(QPixmap());

    // Rich Nexus info, async + cached
    const QString nexusId = entry->nexusModId;
    if (nexusId.isEmpty() || !NexusApi::keyAvailable())
        return;

    // Image: cache hit?
    if (m_imageCache.contains(nexusId))
        applyImage(m_imageCache.value(nexusId));

    // Details: cache hit?
    if (m_detailCache.contains(nexusId)) {
        const NexusApi::ModDetails& d = m_detailCache.value(nexusId);
        applyDetails(d);
        if (!m_imageCache.contains(nexusId) && !d.pictureUrl.isEmpty())
            fetchImage(nexusId, d.pictureUrl);
        return;
    }

    // Miss -> fetch details off the UI thread.
    m_detailsWatcher.setFuture(QtConcurrent::run([nexusId]() {
        return NexusApi::modDetails(nexusId);
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

    QString body = d.summary.isEmpty() ? d.description : d.summary;
    body = stripBBCode(body);
    if (!body.isEmpty())
        m_desc->setPlainText(body);

    if (!d.pictureUrl.isEmpty())
        fetchImage(d.modId, d.pictureUrl);
}

void ModInfoWidget::fetchImage(const QString& nexusModId, const QString& url) {
    if (m_imageCache.contains(nexusModId)) { applyImage(m_imageCache.value(nexusModId)); return; }
    QNetworkRequest req{QUrl(url)};
    QNetworkReply* reply = m_nam->get(req);
    QPointer<ModInfoWidget> self(this);
    const QString fetchedFor = nexusModId;
    connect(reply, &QNetworkReply::finished, this, [self, reply, fetchedFor]() {
        reply->deleteLater();
        if (!self) return;
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        if (!pm.loadFromData(reply->readAll())) return;
        self->m_imageCache.insert(fetchedFor, pm);
        if (fetchedFor == self->m_currentNexusId)
            self->applyImage(pm);
    });
}

void ModInfoWidget::applyImage(const QPixmap& pm) {
    if (pm.isNull()) return;
    m_image->setPixmap(pm.scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

} // namespace solero
