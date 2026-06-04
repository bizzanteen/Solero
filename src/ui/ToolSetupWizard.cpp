#include "ToolSetupWizard.h"
#include "tools/ToolCatalog.h"
#include "tools/ToolDownloader.h"
#include "tools/ToolStore.h"
#include "ui/ProgressModal.h"
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

namespace solero {

ToolSetupWizard::ToolSetupWizard(QWidget* parent, ToolStore* store)
    : QDialog(parent), m_store(store) {
    setWindowTitle("Set Up a Tool");
    resize(640, 420);

    auto* outer = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;

    auto* list = new QListWidget(this);
    list->setMinimumWidth(240);
    for (const auto& p : ToolCatalog::presets()) {
        auto* item = new QListWidgetItem(p.name + " - by " + p.author, list);
        item->setData(Qt::UserRole, p.id);
    }
    body->addWidget(list);

    auto* details = new QVBoxLayout;
    auto* nameLbl = new QLabel(this);
    nameLbl->setWordWrap(true);
    auto* authorLbl = new QLabel(this);
    authorLbl->setWordWrap(true);
    auto* creditLbl = new QLabel(this);
    creditLbl->setWordWrap(true);
    auto* openBtn = new QPushButton("Open mod page", this);
    auto* endorseBtn = new QPushButton("Endorse (coming soon)", this);
    endorseBtn->setEnabled(false);
    details->addWidget(nameLbl);
    details->addWidget(authorLbl);
    details->addWidget(creditLbl);
    details->addStretch();
    details->addWidget(openBtn);
    details->addWidget(endorseBtn);
    body->addLayout(details);
    outer->addLayout(body);

    auto* btnRow = new QHBoxLayout;
    auto* setupBtn = new QPushButton("Set Up Tool", this);
    auto* cancelBtn = new QPushButton("Cancel", this);
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
            nameLbl->clear(); authorLbl->clear(); creditLbl->clear();
            openBtn->setEnabled(false);
            return;
        }
        nameLbl->setText("<h3>" + p->name.toHtmlEscaped() + "</h3>");
        authorLbl->setText("By " + p->author.toHtmlEscaped());
        creditLbl->setText("Please consider endorsing " + p->name.toHtmlEscaped()
                           + " by " + p->author.toHtmlEscaped() + " on Nexus.");
        openBtn->setEnabled(!p->creditUrl.isEmpty());
    };
    connect(list, &QListWidget::currentItemChanged, this, [=]{ updateDetails(); });
    if (list->count() > 0) list->setCurrentRow(0);

    connect(openBtn, &QPushButton::clicked, this, [=]{
        if (const ToolPreset* p = selectedPreset())
            QDesktopServices::openUrl(QUrl(p->creditUrl));
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

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
            QString lad = AppConfig::instance().localAppDataDir();
            int pfx = lad.indexOf("/pfx");
            de.winePrefix = pfx > 0 ? lad.left(pfx) : QString();
            de.runThroughDeployer = true;
            m_store->update(de);
        }

        prog.setMessage("Downloading " + p->name + "\xe2\x80\xa6");
        prog.pump();
        auto res = ToolDownloader::fetch(*p, downloadsDir, toolsRoot, cb);
        prog.close();
        if (!res.ok) {
            QMessageBox::warning(this, "Set Up Tool", res.error);
            return;
        }

        Executable e;
        e.id = p->id;
        e.name = p->name;
        e.binaryPath = res.exePath;
        e.arguments = p->args;
        e.runtime = p->proton ? RuntimeType::Proton : RuntimeType::Native;
        // Wine prefix = the Skyrim Proton prefix (compatdata/489830), derived from
        // localAppData up to /pfx.
        QString lad = AppConfig::instance().localAppDataDir();
        int pfx = lad.indexOf("/pfx");
        e.winePrefix = pfx > 0 ? lad.left(pfx) : QString();
        e.runThroughDeployer = true;
        m_store->update(e); // update = add-or-replace by id
        m_store->save();

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
