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
#include <QPair>
#include <QProcess>
#include <QProcessEnvironment>
#include <QEventLoop>
#include <QStandardPaths>

namespace solero {

// Install the .NET Desktop Runtime into the Skyrim Proton prefix via
// `umu-run winetricks -q dotnetdesktop8`. Synthesis (and other framework-
// dependent .NET apps) crash under Proton without it. Mirrors the Proton env
// that ToolRunner builds. Returns true on a clean (exit 0) install.
static bool installDotNetIntoPrefix(const QString& winePrefix, QWidget* parent) {
    if (winePrefix.isEmpty()) return false; // no prefix -> nothing to do
    if (QStandardPaths::findExecutable("umu-run").isEmpty()) return false;

    QString protonDir = AppConfig::instance().detectProtonDir();
    if (protonDir.isEmpty()) return false;

    // Derive the Steam root the same way ToolRunner does (from the game dir).
    QString steamRoot = QDir(AppConfig::instance().gameDir() + "/../../..").canonicalPath();
    if (steamRoot.isEmpty())
        steamRoot = QDir::homePath() + "/.local/share/Steam";

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("WINEPREFIX", winePrefix + "/pfx");
    env.insert("STEAM_COMPAT_DATA_PATH", winePrefix);
    env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamRoot);
    env.insert("GAMEID", "umu-489830");
    env.insert("STORE", "none");
    env.insert("PROTONPATH", protonDir);
    // No PROTON_VERB needed for winetricks.

    ProgressModal prog(parent, "Installing .NET",
        "Installing the .NET runtime into the Skyrim prefix.\nThis can take several minutes\xe2\x80\xa6");
    prog.show(); prog.pump();

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.setProcessEnvironment(env);

    // Wait via an event loop so the modal stays responsive; rely on the
    // finished signal (this is long) rather than a hard kill.
    QEventLoop loop;
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    QObject::connect(&proc, &QProcess::readyReadStandardOutput, parent, [&]{ prog.pump(); });
    QObject::connect(&proc, &QProcess::readyReadStandardError,  parent, [&]{ prog.pump(); });

    proc.start("umu-run", QStringList{"winetricks", "-q", "dotnetdesktop8"});
    if (!proc.waitForStarted(15000)) { prog.close(); return false; }
    loop.exec();
    prog.close();
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

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
    auto* custom = new QListWidgetItem(QIcon::fromTheme("list-add"),
                                       "Add Custom Tool\xe2\x80\xa6", list);
    custom->setData(Qt::UserRole, "__custom__");
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
    authorLbl->setTextFormat(Qt::RichText);
    authorLbl->setOpenExternalLinks(true);
    auto* descLbl = new QLabel(detailsW);
    descLbl->setWordWrap(true);
    descLbl->setTextFormat(Qt::PlainText);
    auto* docsLbl = new QLabel(detailsW);
    docsLbl->setWordWrap(true);
    docsLbl->setTextFormat(Qt::RichText);
    docsLbl->setOpenExternalLinks(true);
    auto* openBtn = new QPushButton("Open mod page", detailsW);
    auto* endorseBtn = new QPushButton("Endorse (coming soon)", detailsW);
    endorseBtn->setEnabled(false);
    details->addWidget(iconLbl);
    details->addWidget(nameLbl);
    details->addWidget(authorLbl);
    details->addWidget(descLbl);
    details->addWidget(docsLbl);
    details->addStretch();
    details->addWidget(openBtn);
    details->addWidget(endorseBtn);
    body->addWidget(detailsW);
    outer->addLayout(body);

    auto* btnRow = new QHBoxLayout;
    auto* setupBtn = new QPushButton("Set Up Tool", this);
    auto* cancelBtn = new QPushButton("Cancel", this);
    btnRow->addStretch();
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(setupBtn);
    outer->addLayout(btnRow);

    auto selectedId = [list]() -> QString {
        auto* item = list->currentItem();
        return item ? item->data(Qt::UserRole).toString() : QString();
    };
    auto selectedPreset = [list]() -> const ToolPreset* {
        auto* item = list->currentItem();
        if (!item) return nullptr;
        return ToolCatalog::byId(item->data(Qt::UserRole).toString());
    };

    auto updateDetails = [=]() {
        if (selectedId() == "__custom__") {
            iconLbl->setPixmap(QIcon::fromTheme("list-add").pixmap(64, 64));
            nameLbl->setText("<h3>Add a Custom Tool</h3>");
            authorLbl->clear();
            descLbl->setText("Manually point Solero at any executable.");
            docsLbl->clear();
            docsLbl->hide();
            openBtn->setEnabled(false);
            endorseBtn->setEnabled(false);
            return;
        }
        const ToolPreset* p = selectedPreset();
        if (!p) {
            iconLbl->clear();
            nameLbl->clear(); authorLbl->clear(); descLbl->clear(); docsLbl->clear();
            openBtn->setEnabled(false);
            return;
        }
        iconLbl->setPixmap(QPixmap(p->iconResource));
        nameLbl->setText("<h3>" + p->name.toHtmlEscaped() + "</h3>");
        if (p->authorUrl.isEmpty())
            authorLbl->setText("By " + p->author.toHtmlEscaped());
        else
            authorLbl->setText(QString("By <a href=\"%1\">%2</a>")
                                   .arg(p->authorUrl.toHtmlEscaped(), p->author.toHtmlEscaped()));
        descLbl->setText(p->description);
        if (p->docsUrl.isEmpty()) {
            docsLbl->clear();
            docsLbl->hide();
        } else {
            docsLbl->setText(QString("<a href=\"%1\">Documentation \xe2\x86\x97</a>")
                                 .arg(p->docsUrl.toHtmlEscaped()));
            docsLbl->show();
        }
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
        if (selectedId() == "__custom__") {
            ExecutableDialog dlg({}, this);
            dlg.setOutputModChoices(m_modChoices, QString());
            if (dlg.exec() == QDialog::Accepted) {
                auto e = dlg.result();
                if (e.id.isEmpty()) e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                m_store->update(e); m_store->save();
                accept();
            }
            return;
        }
        const ToolPreset* p = selectedPreset();
        if (!p) return;

        const QString downloadsDir = AppConfig::instance().downloadsDir();
        const QString toolsRoot = AppConfig::instance().toolsDir();

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
            de.runThroughDeployer = false;
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
        e.runThroughDeployer = false;

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

        // Framework-dependent .NET apps (e.g. Synthesis) crash under Proton
        // without the .NET Desktop Runtime in the prefix. Install it now.
        bool dotNetInstalled = false;
        if (p->needsDotNet && e.runtime == RuntimeType::Proton) {
            if (installDotNetIntoPrefix(e.winePrefix, this)) {
                dotNetInstalled = true;
            } else {
                QMessageBox::warning(this, "Set Up Tool",
                    "The tool is set up, but installing the .NET runtime failed. "
                    "Synthesis may not launch until .NET is installed in the prefix "
                    "(e.g. `protontricks 489830 dotnetdesktop8`).");
            }
        }

        QMessageBox box(this);
        box.setWindowTitle("Tool Ready");
        box.setText(p->name + " is set up." + (dotNetInstalled ? QString("\n.NET runtime installed.") : QString()));
        box.setInformativeText("Thanks to " + p->author + " for making it. If you find it useful, please consider endorsing it on Nexus.");
        auto* endorseBtn = box.addButton(QString::fromUtf8("\xe2\x99\xa5  Endorse and Close"), QMessageBox::AcceptRole);
        auto* closeBtn   = box.addButton(QString::fromUtf8("\xe2\x98\xb9  Close"), QMessageBox::RejectRole);
        box.exec();
        // Endorsing isn't wired yet (deferred to the Nexus phase); both just close for now.
        Q_UNUSED(endorseBtn); Q_UNUSED(closeBtn);
        accept();
    });
}

void ToolSetupWizard::setModChoices(const QList<QPair<QString,QString>>& choices) {
    m_modChoices = choices;
}

void ToolSetupWizard::run(QWidget* parent, ToolStore* store) {
    ToolSetupWizard dlg(parent, store);
    dlg.exec();
}

}
