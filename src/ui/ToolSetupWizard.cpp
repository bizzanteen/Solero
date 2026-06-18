#include "ToolSetupWizard.h"
#include "tools/ToolCatalog.h"
#include "tools/ToolDownloader.h"
#include "tools/ToolSetup.h"
#include "tools/ToolStore.h"
#include "ui/ProgressModal.h"
#include "ui/ExecutableDialog.h"
#include "nexus/NexusApi.h"
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

// Build the Proton/umu environment for running tools inside the Skyrim prefix.
// Mirrors what ToolRunner constructs. Returns an empty environment (and the
// caller should bail) if the prefix is unusable; check protonDir separately.
// True if `path` exists and contains at least one entry. Used to VERIFY a .NET
// install actually landed, since umu-run/winetricks (and Windows installers under
// Proton) frequently exit non-zero even on success.
static bool dirHasEntries(const QString& path) {
    QDir d(path);
    return d.exists() && !d.entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot).isEmpty();
}

static QProcessEnvironment dotnetPrefixEnv(const QString& winePrefix) {
    QString protonDir = AppConfig::instance().detectProtonDir();

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
    // No PROTON_VERB needed for winetricks / silent installers.
    return env;
}

// Install the .NET Desktop Runtime into the Skyrim Proton prefix via
// `umu-run winetricks -q dotnetdesktop8`. Synthesis (and other framework-
// dependent .NET apps) crash under Proton without it. Mirrors the Proton env
// that ToolRunner builds. Returns true on a clean (exit 0) install.
static bool installDotNetIntoPrefix(const QString& winePrefix, QWidget* parent) {
    if (winePrefix.isEmpty()) return false; // no prefix -> nothing to do
    if (QStandardPaths::findExecutable("umu-run").isEmpty()) return false;

    QString protonDir = AppConfig::instance().detectProtonDir();
    if (protonDir.isEmpty()) return false;

    QProcessEnvironment env = dotnetPrefixEnv(winePrefix);

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
    const QString out = QString::fromUtf8(proc.readAll());
    prog.close();
    // Don't trust the exit code: umu-run/winetricks exit non-zero even on success
    // (and exit 1 with "already installed"). Verify the runtime actually landed.
    return proc.exitCode() == 0
        || out.contains("already installed", Qt::CaseInsensitive)
        || dirHasEntries(winePrefix + "/pfx/drive_c/Program Files/dotnet/shared/Microsoft.WindowsDesktop.App");
}

// Install the full .NET SDK into the Skyrim Proton prefix. Synthesis builds
// patchers with the .NET SDK (its BuildHost/MSBuild), so the Desktop Runtime
// alone is not enough. winetricks has no modern-SDK verb, so we download the
// official Windows SDK installer and run it silently through Proton. Returns
// true on a clean (exit 0) install.
static bool installDotNetSdkIntoPrefix(const QString& winePrefix, QWidget* parent) {
    if (winePrefix.isEmpty()) return false; // no prefix -> nothing to do
    if (QStandardPaths::findExecutable("umu-run").isEmpty()) return false;
    if (AppConfig::instance().detectProtonDir().isEmpty()) return false;

    // Resolve the latest 8.0 SDK version, then the win-x64 installer URL.
    QString ver;
    {
        QProcess curlVer;
        curlVer.start("curl", QStringList{"-fsSL",
            "https://dotnetcli.blob.core.windows.net/dotnet/Sdk/8.0/latest.version"});
        if (curlVer.waitForFinished(15000))
            ver = QString::fromUtf8(curlVer.readAllStandardOutput()).trimmed();
    }
    if (ver.isEmpty()) return false; // caller handles user-facing messaging

    const QString url = "https://dotnetcli.blob.core.windows.net/dotnet/Sdk/"
        + ver + "/dotnet-sdk-" + ver + "-win-x64.exe";
    const QString dest =
        AppConfig::instance().downloadsDir() + "/dotnet-sdk-win-x64.exe";

    ProgressModal prog(parent, "Installing .NET SDK",
        "Downloading the .NET SDK (~220 MB)\xe2\x80\xa6");
    prog.show(); prog.pump();

    // Download the installer
    {
        QProcess dl;
        dl.setProcessChannelMode(QProcess::MergedChannels);
        QEventLoop loop;
        QObject::connect(&dl, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         &loop, &QEventLoop::quit);
        QObject::connect(&dl, &QProcess::readyReadStandardOutput, parent, [&]{ prog.pump(); });
        QObject::connect(&dl, &QProcess::readyReadStandardError,  parent, [&]{ prog.pump(); });

        dl.start("curl", QStringList{"-L", "--fail", "-o", dest, url});
        if (!dl.waitForStarted(15000)) { prog.close(); return false; }
        loop.exec();

        bool dlOk = dl.exitStatus() == QProcess::NormalExit && dl.exitCode() == 0
                    && QFileInfo(dest).size() > 0;
        if (!dlOk) {
            QFile::remove(dest); // drop any partial download
            prog.close();
            return false;
        }
    }

    // Run the SDK installer silently inside the prefix
    prog.setMessage("Installing the .NET SDK into the Skyrim prefix.\n"
                    "This can take several minutes\xe2\x80\xa6");
    prog.pump();

    QProcessEnvironment env = dotnetPrefixEnv(winePrefix);

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.setProcessEnvironment(env);

    QEventLoop loop;
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    QObject::connect(&proc, &QProcess::readyReadStandardOutput, parent, [&]{ prog.pump(); });
    QObject::connect(&proc, &QProcess::readyReadStandardError,  parent, [&]{ prog.pump(); });

    proc.start("umu-run", QStringList{dest, "/install", "/quiet", "/norestart"});
    if (!proc.waitForStarted(15000)) { prog.close(); return false; }
    loop.exec();
    prog.close();
    // Verify by the installed SDK folder, not the installer's exit code (Windows
    // installers under Proton can return non-zero on success).
    const bool installed = dirHasEntries(winePrefix + "/pfx/drive_c/Program Files/dotnet/sdk");
    // Drop the ~220 MB installer once the SDK is verifiably in place; keep it if
    // the install couldn't be verified so it can be retried without re-downloading.
    if (installed) QFile::remove(dest);
    return installed;
}

// Endorse a Nexus-sourced tool preset and report the outcome via a QMessageBox.
// No-op (with a message) for GitHub tools, missing modId, or no API key.
static void endorsePreset(const ToolPreset* p, QWidget* parent) {
    if (!p || p->source != ToolSource::Nexus || p->nexusModId.isEmpty()) {
        QMessageBox::information(parent, "Endorse",
            "This tool isn't on Nexus, so there's nothing to endorse.");
        return;
    }
    if (!NexusApi::keyAvailable()) {
        QMessageBox::warning(parent, "Endorse",
            "A Nexus account is required to endorse mods.\n"
            "Connect your Nexus account in Settings \xe2\x80\xba Nexus Account.");
        return;
    }
    QString version = NexusApi::modInfo(p->nexusModId, p->nexusGame).version;
    auto res = NexusApi::endorse(p->nexusModId, version, false, p->nexusGame);
    if (res.ok)
        QMessageBox::information(parent, "Endorse",
            "Thanks - endorsed " + p->name + "!");
    else
        QMessageBox::warning(parent, "Endorse",
            res.message.isEmpty() ? QString("Could not endorse this tool.") : res.message);
}

ToolSetupWizard::ToolSetupWizard(QWidget* parent, ToolStore* store,
                                 const QSet<QString>& installedKeys)
    : QDialog(parent), m_installedKeys(installedKeys), m_store(store) {
    setWindowTitle("Set Up a Tool");
    setFixedSize(720, 480);

    auto* outer = new QVBoxLayout(this);
    auto* body = new QHBoxLayout;

    auto* list = new QListWidget(this);
    list->setIconSize(QSize(32, 32));
    list->setMinimumWidth(260);
    list->setMaximumWidth(260);
    // A preset is "installed" PER profile: its id OR (case-insensitive) name is in
    // the active profile's installed set. The global m_store is intentionally not
    // consulted here - it's a shared template, so reading it would mark a tool
    // "installed" in every profile.
    auto isInstalled = [this](const QString& id, const QString& name) {
        return m_installedKeys.contains(id) || m_installedKeys.contains(name.toLower());
    };
    for (const auto& p : ToolCatalog::presets()) {
        auto* item = new QListWidgetItem(QIcon(p.iconResource), p.name, list);
        item->setData(Qt::UserRole, p.id);
        if (isInstalled(p.id, p.name)) {
            item->setText(p.name + " (installed)");
            item->setToolTip("Already set up.");
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        }
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
    details->addWidget(iconLbl);
    details->addWidget(nameLbl);
    details->addWidget(authorLbl);
    details->addWidget(descLbl);
    details->addWidget(docsLbl);
    details->addStretch();
    details->addWidget(openBtn);
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
    // Disabled (already-installed) items can't be selected by click, so start on
    // the first enabled row (the "Add Custom Tool…" item is always enabled).
    for (int i = 0; i < list->count(); ++i) {
        if (list->item(i)->flags() & Qt::ItemIsEnabled) {
            list->setCurrentRow(i);
            break;
        }
    }

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
        // Belt-and-suspenders: never re-set-up an already-installed preset, even
        // if a disabled item somehow became the current selection.
        if (isInstalled(p->id, p->name)) return;

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

        // Wine prefix = the Skyrim Proton prefix (compatdata/489830), derived from
        // localAppData up to /pfx.
        QString lad = AppConfig::instance().localAppDataDir();
        int pfx = lad.indexOf("/pfx");
        const QString winePrefix = pfx > 0 ? lad.left(pfx) : QString();

        // Build the Executable (incl. extra actions) via the shared headless
        // helper, then override the icon with the freshly-downloaded one if any.
        Executable e = ToolSetup::buildExecutable(*p, res.exePath, winePrefix);
        if (!res.iconPath.isEmpty()) e.iconPath = res.iconPath;

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
        // without the .NET Desktop Runtime in the prefix. Synthesis also builds
        // patchers with the full .NET SDK (its BuildHost/MSBuild), so install
        // both the runtime and the SDK now.
        bool dotNetOk = false;
        if (p->needsDotNet && e.runtime == RuntimeType::Proton) {
            bool runtimeOk = installDotNetIntoPrefix(e.winePrefix, this);
            bool sdkOk = installDotNetSdkIntoPrefix(e.winePrefix, this);
            dotNetOk = runtimeOk && sdkOk;
            if (!dotNetOk) {
                QString which;
                if (!runtimeOk && !sdkOk) which = "the .NET runtime and SDK";
                else if (!runtimeOk)      which = "the .NET runtime";
                else                       which = "the .NET SDK";
                QMessageBox::warning(this, "Set Up Tool",
                    "The tool is set up, but installing " + which + " failed. "
                    "Synthesis may not launch or build patchers until both the "
                    ".NET runtime and SDK are installed in the prefix. Install the "
                    "runtime with `protontricks 489830 dotnetdesktop8`, and the SDK "
                    "by running the dotnet-sdk win-x64 installer through the prefix.");
            }
        }

        QMessageBox box(this);
        box.setWindowTitle("Tool Ready");
        box.setText(p->name + " is set up." + (dotNetOk ? QString("\n.NET runtime + SDK installed.") : QString()));
        box.setInformativeText("Thanks to " + p->author + " for making it. If you find it useful, please consider endorsing it on Nexus.");
        // Only Nexus tools can be endorsed; GitHub tools just get a Close button.
        const bool canEndorse = p->source == ToolSource::Nexus && !p->nexusModId.isEmpty();
        QPushButton* endorseBtn = canEndorse
            ? box.addButton(QString::fromUtf8("\xe2\x99\xa5  Endorse and Close"), QMessageBox::AcceptRole)
            : nullptr;
        auto* closeBtn   = box.addButton(QString::fromUtf8("\xe2\x98\xb9  Close"), QMessageBox::RejectRole);
        box.exec();
        Q_UNUSED(closeBtn);
        if (endorseBtn && box.clickedButton() == endorseBtn)
            endorsePreset(p, this);
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
