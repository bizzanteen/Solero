#include "PatchWizardDialog.h"
#include "ProgressModal.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include "install/ModInstaller.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QIcon>

namespace solero {

PatchWizardDialog::PatchWizardDialog(Profile* profile, QWidget* parent)
    : QDialog(parent), m_profile(profile) {
    setWindowTitle("Patch Wizard");
    resize(640, 520);

    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(
        "Re-scans installed FOMOD mods, reconstructs your original choices, and "
        "compares them against your current load order. Surfaces patches that are "
        "now applicable (a matching mod or plugin is present) but were not installed "
        "- including \"pick which mod you have\" options. Tick the ones to install.", this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* container = new QWidget(scroll);
    m_listLayout = new QVBoxLayout(container);
    m_listLayout->setAlignment(Qt::AlignTop);
    scroll->setWidget(container);
    root->addWidget(scroll, 1);

    auto* buttons = new QDialogButtonBox(this);
    m_installBtn = buttons->addButton("Install Selected", QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(m_installBtn, &QPushButton::clicked, this, &PatchWizardDialog::onInstallSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    runScan();
    buildList();
}

void PatchWizardDialog::runScan() {
    ProgressModal prog(this, "Patch Wizard", "Scanning installed FOMOD mods\xe2\x80\xa6");
    prog.show();
    prog.pump();
    const QString gameDir = AppConfig::instance().gameDir();
    const QString staging = AppConfig::instance().stagingDir();
    m_candidates = scanProfile(*m_profile, gameDir, staging, [&](const QString& name) {
        prog.setMessage("Scanning: " + name);
        prog.pump();
    });
    prog.close();
}

void PatchWizardDialog::buildList() {
    if (m_candidates.isEmpty()) {
        auto* empty = new QLabel("No applicable patches found.", this);
        empty->setAlignment(Qt::AlignCenter);
        m_listLayout->addWidget(empty);
        m_installBtn->setEnabled(false);
        return;
    }

    QString lastMod;
    for (const PatchCandidate& c : m_candidates) {
        if (c.modName != lastMod) {
            lastMod = c.modName;
            auto* header = new QLabel("<b>" + c.modName.toHtmlEscaped() + "</b>", this);
            m_listLayout->addWidget(header);
        }
        auto* row = new QWidget(this);
        auto* rl = new QVBoxLayout(row);
        rl->setContentsMargins(16, 2, 4, 6);
        rl->setSpacing(1);
        auto* check = new QCheckBox(c.optionName, row);
        if (c.installable) {
            check->setChecked(true);
        } else {
            check->setChecked(false);
            check->setEnabled(false);
            check->setText(c.optionName + "  (detected - install needs the source archive)");
        }
        rl->addWidget(check);
        auto* reason = new QLabel("<i>" + c.reason.toHtmlEscaped() + "</i>", row);
        reason->setStyleSheet("color: palette(mid);");
        reason->setContentsMargins(20, 0, 0, 0);
        rl->addWidget(reason);
        if (!c.optionDescription.isEmpty()) {
            auto* desc = new QLabel(c.optionDescription, row);
            desc->setWordWrap(true);
            desc->setContentsMargins(20, 0, 0, 0);
            rl->addWidget(desc);
        }
        // Summarise the files/folders this patch would install.
        QStringList paths;
        for (const FomodFile& f : c.files) {
            const QString d = f.destination.isEmpty()
                ? (f.isFolder ? QStringLiteral("Data/") : f.source) : f.destination;
            paths << (f.isFolder ? (d + "/*") : d);
        }
        if (!paths.isEmpty()) {
            const int shown = qMin(paths.size(), 8);
            QString text = paths.mid(0, shown).join(", ");
            if (paths.size() > shown)
                text += QStringLiteral(" \xe2\x80\xa6 (+%1 more)").arg(paths.size() - shown);
            auto* files = new QLabel(text.toHtmlEscaped(), row);
            files->setWordWrap(true);
            files->setStyleSheet("color: palette(mid); font-size: 11px;");
            files->setContentsMargins(20, 0, 0, 0);
            rl->addWidget(files);
        }
        m_listLayout->addWidget(row);
        m_checks.append(check);
    }
}

void PatchWizardDialog::onInstallSelected() {
    QStringList changed;
    int installed = 0;
    ProgressModal prog(this, "Patch Wizard", "Installing patches\xe2\x80\xa6");
    prog.show();
    prog.pump();
    const QString staging = AppConfig::instance().stagingDir();
    for (int i = 0; i < m_candidates.size(); ++i) {
        if (i >= m_checks.size() || !m_checks[i]->isChecked()) continue;
        const PatchCandidate& c = m_candidates[i];
        if (!c.installable || c.sourceArchive.isEmpty()) continue;
        prog.setMessage("Installing: " + c.optionName);
        prog.pump();
        const QString modDir = staging + "/" + c.modId;
        if (ModInstaller::installOptionFiles(c.sourceArchive, modDir, c.files)) {
            ++installed;
            if (!changed.contains(c.modId)) changed.append(c.modId);
        }
    }
    prog.close();

    if (installed == 0) {
        QMessageBox::information(this, "Patch Wizard",
            "No patches were selected, or none could be installed.");
        return;
    }
    emit patchesInstalled(changed);
    QMessageBox::information(this, "Patch Wizard",
        QString("Installed %1 patch%2 into %3 mod%4. Re-deploy to apply.")
            .arg(installed).arg(installed == 1 ? "" : "es")
            .arg(changed.size()).arg(changed.size() == 1 ? "" : "s"));
    accept();
}

} // namespace solero
