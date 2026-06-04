#include "ToolSetupWizard.h"
#include "tools/ToolCatalog.h"
#include "tools/ToolDownloader.h"
#include "tools/ToolStore.h"
#include "ui/ProgressModal.h"
#include "ui/ExecutableDialog.h"
#include "core/AppConfig.h"
#include "core/Types.h"
#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QIcon>
#include <QPixmap>

namespace solero {

ToolSetupWizard::ToolSetupWizard(QWidget* parent, ToolStore* store)
    : QDialog(parent), m_store(store) {
    setWindowTitle("Set Up a Tool");
    setFixedSize(720, 480);

    auto* outer = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;

    auto* list = new QListWidget(this);
    list->setIconSize(QSize(32, 32));
    list->setMinimumWidth(260);
    list->setMaximumWidth(260);
    for (const auto& p : ToolCatalog::presets()) {
        auto* item = new QListWidgetItem(QIcon(p.iconResource), p.name, list);
        item->setData(Qt::UserRole, p.id);
    }
    body->addWidget(list);

    auto* detailsW = new QWidget(this);
    detailsW->setFixedWidth(420);
    auto* details = new QVBoxLayout(detailsW);

    auto* iconLbl = new QLabel(detailsW);
    iconLbl->setFixedSize(64, 64);
    iconLbl->setScaledContents(true);
    auto* nameLbl = new QLabel(detailsW);
    nameLbl->setWordWrap(true);
    auto* authorLbl = new QLabel(detailsW);
    authorLbl->setWordWrap(true);
    authorLbl->setTextFormat(Qt::PlainText);
    auto* creditLbl = new QLabel(detailsW);
    creditLbl->setWordWrap(true);
    creditLbl->setTextFormat(Qt::PlainText);
    auto* openBtn = new QPushButton("Open mod page", detailsW);
    auto* endorseBtn = new QPushButton("Endorse (coming soon)", detailsW);
    endorseBtn->setEnabled(false);
    details->addWidget(iconLbl);
    details->addWidget(nameLbl);
    details->addWidget(authorLbl);
    details->addWidget(creditLbl);
    details->addStretch();
    details->addWidget(openBtn);
    details->addWidget(endorseBtn);
    body->addWidget(detailsW);
    outer->addLayout(body);

    auto* btnRow = new QHBoxLayout;
    auto* customBtn = new QPushButton("Custom Tool\xe2\x80\xa6", this);
    auto* setupBtn = new QPushButton("Set Up Tool", this);
    auto* cancelBtn = new QPushButton("Cancel", this);
    btnRow->addWidget(customBtn);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(setupBtn);
    outer->addLayout(btnRow);

    auto selectedPreset = [list]() -> const ToolPreset* {
        auto* item = list->currentItem();
        if (!item) return nullptr;
        return ToolCatalog::byId(item->data(Qt::UserRole).toString());
    };

    auto updateDetails = [=]() {
        const ToolPreset* p = selectedPreset();
        if (!p) {
            iconLbl->clear();
            nameLbl->clear(); authorLbl->clear(); creditLbl->clear();
            openBtn->setEnabled(false);
            return;
        }
        iconLbl->setPixmap(QPixmap(p->iconResource));
        nameLbl->setText("<h3>" + p->name.toHtmlEscaped() + "</h3>");
        authorLbl->setText("By " + p->author);
        creditLbl->setText("Please consider endorsing " + p->name
                           + " by " + p->author + " on Nexus.");
        openBtn->setEnabled(!p->creditUrl.isEmpty());
    };
    connect(list, &QListWidget::currentItemChanged, this, [=]{ updateDetails(); });
    if (list->count() > 0) list->setCurrentRow(0);

    connect(openBtn, &QPushButton::clicked, this, [=]{
        if (const ToolPreset* p = selectedPreset())
            QDesktopServices::openUrl(QUrl(p->creditUrl));
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(customBtn, &QPushButton::clicked, this, [this]{
        ExecutableDialog dlg({}, this);
        if (dlg.exec() == QDialog::Accepted) {
            Executable e = dlg.result();
            if (e.id.isEmpty()) e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_store->update(e); m_store->save();
            accept();
        }
    });

    connect(setupBtn, &QPushButton::clicked, this, [=]{
        const ToolPreset* p = selectedPreset();
        if (!p) return;

        const QString downloadsDir = AppConfig::instance().downloadsDir();
        const QString toolsRoot = QDir::homePath() + "/Modding/Tools";

        ProgressModal prog(this, "Set Up Tool", "Downloading " + p->name + "\xe2\x80\xa6");
        prog.show(); prog.pump();
        auto cb = [&](int pct){ prog.setProgress(pct, 100); prog.pump(); };

        // Resolve dependencies first: fetch each need whose exe isn't already registered.
        for (const QString& needId : p->needs) {
            const ToolPreset* dep = ToolCatalog::byId(needId);
            if (!dep) continue;
            bool already = false;
            for (const auto& t : m_store->tools()) if (t.id == dep->id) { already = true; break; }
            if (already) continue;
            prog.setMessage("Downloading dependency " + dep->name + "\xe2\x80\xa6");
            prog.pump();
            auto depRes = ToolDownloader::fetch(*dep, downloadsDir, toolsRoot, cb);
            if (!depRes.ok) {
                prog.close();
                QMessageBox::warning(this, "Set Up Tool",
                    "Dependency '" + dep->name + "' failed: " + depRes.error);
                return;
            }
            // Register the dependency too (so it appears as a tool / isn't re-fetched).
            Executable de;
            de.id = dep->id;
            de.name = dep->name;
            de.binaryPath = depRes.exePath;
            de.arguments = dep->args;
            de.runtime = dep->proton ? RuntimeType::Proton : RuntimeType::Native;
            de.iconPath = depRes.iconPath.isEmpty() ? dep->iconResource : depRes.iconPath;
            QString lad = AppConfig::instance().localAppDataDir();
            int pfx = lad.indexOf("/pfx");
            de.winePrefix = pfx > 0 ? lad.left(pfx) : QString();
            de.runThroughDeployer = true;
            m_store->update(de);
        }

        prog.setMessage("Downloading " + p->name + "\xe2\x80\xa6");
        prog.pump();
        auto res = ToolDownloader::fetch(*p, downloadsDir, toolsRoot, cb);
        if (!res.ok) {
            prog.close();
            QMessageBox::warning(this, "Set Up Tool", res.error);
            return;
        }

        Executable e;
        e.id = p->id;
        e.name = p->name;
        e.binaryPath = res.exePath;
        e.arguments = p->args;
        e.runtime = p->proton ? RuntimeType::Proton : RuntimeType::Native;
        e.iconPath = res.iconPath.isEmpty() ? p->iconResource : res.iconPath;
        e.protonVersion = QFileInfo(AppConfig::instance().detectProtonDir()).fileName();
        // Wine prefix = the Skyrim Proton prefix (compatdata/489830), derived from
        // localAppData up to /pfx.
        QString lad = AppConfig::instance().localAppDataDir();
        int pfx = lad.indexOf("/pfx");
        e.winePrefix = pfx > 0 ? lad.left(pfx) : QString();
        e.runThroughDeployer = true;

        // Build extra actions: resolve each secondary exe in the same install dir.
        QString instDir = QFileInfo(res.exePath).path();
        for (const auto& pa : p->extraActions) {
            QString actExe = instDir + "/" + pa.exeRelPath;
            if (!QFile::exists(actExe)) { // case-insensitive shallow search
                for (const QString& f : QDir(instDir).entryList(QDir::Files))
                    if (f.compare(pa.exeRelPath, Qt::CaseInsensitive) == 0) {
                        actExe = instDir + "/" + f;
                        break;
                    }
            }
            ToolAction a;
            a.label = pa.label;
            a.binaryPath = actExe;
            a.arguments = pa.args;
            a.outputModId = QString();
            e.extraActions.append(a);
        }

        m_store->update(e); // update = add-or-replace by id
        m_store->save();

        // DynDOLOD Resources is installed as a mod, never run.
        if (p->id == "dyndolod") {
            prog.setMessage("Downloading DynDOLOD Resources\xe2\x80\xa6");
            prog.pump();
            ToolPreset resPreset;
            resPreset.id = "dyndolod-resources";
            resPreset.source = ToolSource::Nexus;
            resPreset.nexusGame = "skyrimspecialedition";
            resPreset.nexusModId = ToolCatalog::dyndolodResourcesModId();
            QString resUrl = ToolDownloader::nexusDownloadUrl(resPreset);
            if (!resUrl.isEmpty()) {
                QString resArchive = downloadsDir + "/dyndolod-resources.7z";
                if (resUrl.contains(".zip")) resArchive = downloadsDir + "/dyndolod-resources.zip";
                if (ToolDownloader::curlDownload(resUrl, resArchive,
                                                 "apikey: " + ToolDownloader::nexusApiKey(), cb))
                    emit installModRequested(resArchive);
            }
        }

        prog.close();

        QMessageBox::information(this, "Tool Ready",
            p->name + " is set up.\n\nThanks to " + p->author
            + " - please consider endorsing it on Nexus.");
        accept();
    });
}

void ToolSetupWizard::run(QWidget* parent, ToolStore* store) {
    ToolSetupWizard dlg(parent, store);
    dlg.exec();
}

}
