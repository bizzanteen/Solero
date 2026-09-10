#include "NexusIdentifyDialog.h"
#include "nexus/NexusApi.h"
#include "nexus/FileVersionPick.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QApplication>

namespace solero {
namespace {
constexpr int kModIdRole  = Qt::UserRole;
constexpr int kFileIdRole = Qt::UserRole;
constexpr int kVersionRole = Qt::UserRole + 1;
}

NexusIdentifyDialog::NexusIdentifyDialog(const QString& modName,
                                         const QString& installedVersion,
                                         const QString& knownModId,
                                         QWidget* parent)
    : QDialog(parent), m_installedVersion(installedVersion) {
    setWindowTitle(tr("Identify on Nexus"));
    resize(620, 560);

    auto* v = new QVBoxLayout(this);

    auto* intro = new QLabel(
        knownModId.isEmpty()
            ? tr("This mod couldn't be matched by its archive, so search for it by "
                 "name and pick the file it was installed from.")
            : tr("Solero knows which mod this is but not which file. Pick the file "
                 "it was installed from, or search again to change the mod."), this);
    intro->setWordWrap(true);
    v->addWidget(intro);

    auto* row = new QHBoxLayout;
    m_query = new QLineEdit(modName, this);
    m_query->setClearButtonEnabled(true);
    auto* go = new QPushButton(tr("Search"), this);
    go->setDefault(false);
    go->setAutoDefault(false);
    row->addWidget(m_query, 1);
    row->addWidget(go);
    v->addLayout(row);

    v->addWidget(new QLabel(tr("Matching mods:"), this));
    m_mods = new QListWidget(this);
    v->addWidget(m_mods, 2);

    v->addWidget(new QLabel(tr("Files on the selected mod:"), this));
    m_files = new QListWidget(this);
    v->addWidget(m_files, 3);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet("color:#888;");
    v->addWidget(m_status);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_ok = box->button(QDialogButtonBox::Ok);
    m_ok->setText(tr("Use this file"));
    v->addWidget(box);

    connect(go, &QPushButton::clicked, this, &NexusIdentifyDialog::runSearch);
    connect(m_query, &QLineEdit::returnPressed, this, &NexusIdentifyDialog::runSearch);
    connect(m_mods, &QListWidget::currentRowChanged, this, [this](int){ loadFiles(); });
    connect(m_files, &QListWidget::currentRowChanged, this, [this](int){ updateOkState(); });
    connect(m_files, &QListWidget::itemDoubleClicked, this, [this]{ if (m_ok->isEnabled()) accept(); });
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updateOkState();
    if (knownModId.isEmpty()) {
        runSearch();
    } else {
        auto* it = new QListWidgetItem(modName, m_mods);
        it->setData(kModIdRole, knownModId);
        m_mods->setCurrentRow(0);   // loads its files
    }
}

void NexusIdentifyDialog::runSearch() {
    const QString q = m_query->text().trimmed();
    m_mods->clear();
    m_files->clear();
    updateOkState();
    if (q.isEmpty()) { m_status->setText(tr("Type a name to search for.")); return; }

    m_status->setText(tr("Searching Nexus…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto results = NexusApi::search(q);
    QApplication::restoreOverrideCursor();

    if (results.isEmpty()) {
        m_status->setText(tr("No mods matched \"%1\". Try a shorter or different name.").arg(q));
        return;
    }
    for (const auto& r : results) {
        auto* it = new QListWidgetItem(
            r.author.isEmpty() ? r.name : tr("%1  -  by %2").arg(r.name, r.author), m_mods);
        it->setToolTip(r.summary);
        it->setData(kModIdRole, r.modId);
    }
    m_status->setText(tr("%1 result(s). Pick the mod, then the file.").arg(results.size()));
    m_mods->setCurrentRow(0);
}

void NexusIdentifyDialog::loadFiles() {
    m_files->clear();
    updateOkState();
    auto* sel = m_mods->currentItem();
    if (!sel) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto files = NexusApi::files(sel->data(kModIdRole).toString());
    QApplication::restoreOverrideCursor();

    if (files.isEmpty()) {
        m_status->setText(tr("That mod has no downloadable files listed."));
        return;
    }
    for (const auto& f : files) {
        auto* it = new QListWidgetItem(
            tr("v%1   [%2]   %3").arg(f.version, f.category, f.name), m_files);
        it->setData(kFileIdRole, f.fileId);
        it->setData(kVersionRole, f.version);
    }
    const int pick = bestFileIndexForVersion(files, m_installedVersion);
    m_files->setCurrentRow(pick);
    if (!m_installedVersion.isEmpty()
        && files.at(pick).version.compare(m_installedVersion, Qt::CaseInsensitive) == 0)
        m_status->setText(tr("Preselected the file matching the installed version (%1).")
                              .arg(m_installedVersion));
    else
        m_status->setText(tr("No file matches the installed version (%1) - check the pick.")
                              .arg(m_installedVersion.isEmpty() ? tr("unknown") : m_installedVersion));
}

void NexusIdentifyDialog::updateOkState() {
    auto* mod = m_mods ? m_mods->currentItem() : nullptr;
    auto* file = m_files ? m_files->currentItem() : nullptr;
    const bool ready = mod && file;
    if (m_ok) m_ok->setEnabled(ready);
    if (!ready) { m_modId.clear(); m_fileId.clear(); m_version.clear(); return; }
    m_modId   = mod->data(kModIdRole).toString();
    m_fileId  = file->data(kFileIdRole).toString();
    m_version = file->data(kVersionRole).toString();
}

} // namespace solero
