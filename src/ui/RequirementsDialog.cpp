#include "RequirementsDialog.h"
#include "install/ProtonRuntime.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFrame>

namespace solero {

RequirementsDialog::RequirementsDialog(const QString& dependentName,
                                       const QList<Item>& missing, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Missing requirements");
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);

    auto* header = new QLabel(
        QString("<b>%1</b> requires mods that aren't installed:").arg(dependentName), this);
    header->setWordWrap(true);
    layout->addWidget(header);

    bool anyInstallable = false;
    for (const auto& item : missing) {
        auto* rowFrame = new QFrame(this);
        auto* row = new QHBoxLayout(rowFrame);
        row->setContentsMargins(0, 2, 0, 2);

        // Off-site requirements that are really Windows runtimes (VC++, .NET, …)
        // are shipped by Proton in the game prefix, so label them as such instead
        // of as an installable mod.
        const QString runtimeName = item.external
            ? protonProvidedRuntime(item.url, item.modName, item.notes) : QString();

        // Name (+ optional notes as a dim secondary line). Off-site requirements
        // have no usable Nexus name/id (modId is "0"), so show the off-site page as
        // a clickable link, falling back to its name/notes.
        auto* nameBox = new QVBoxLayout();
        nameBox->setContentsMargins(0, 0, 0, 0);
        nameBox->setSpacing(0);
        auto* nameLbl = new QLabel(rowFrame);
        if (item.external) {
            const QString label = !runtimeName.isEmpty() ? runtimeName
                                : !item.modName.isEmpty() ? item.modName
                                : !item.notes.isEmpty()   ? item.notes
                                                          : item.url;
            if (!item.url.isEmpty()) {
                nameLbl->setText(QString("<a href=\"%1\">%2</a>")
                                     .arg(item.url.toHtmlEscaped(), label.toHtmlEscaped()));
                nameLbl->setTextFormat(Qt::RichText);
                nameLbl->setOpenExternalLinks(true);
                nameLbl->setToolTip(item.url);
            } else {
                nameLbl->setText(label);
            }
        } else {
            nameLbl->setText(item.modName.isEmpty() ? item.modId : item.modName);
        }
        nameBox->addWidget(nameLbl);

        if (!runtimeName.isEmpty()) {
            // Recognised runtime: reassure the user it's normally already handled,
            // and point at protontricks for the rare case it isn't.
            auto* hint = new QLabel(
                QString("Usually provided by Proton - no action normally needed. "
                        "If a mod that needs it misbehaves, check it's configured in "
                        "<a href=\"%1\">protontricks</a>.").arg(protontricksDocsUrl()),
                rowFrame);
            hint->setWordWrap(true);
            hint->setTextFormat(Qt::RichText);
            hint->setOpenExternalLinks(true);
            hint->setStyleSheet("color: gray; font-size: 11px;");
            nameBox->addWidget(hint);
        } else if (!item.notes.isEmpty() && !item.external) {
            // Notes render as a dim secondary line - for off-site rows they're
            // already folded into the label above, so don't repeat them.
            auto* notesLbl = new QLabel(item.notes, rowFrame);
            notesLbl->setWordWrap(true);
            notesLbl->setStyleSheet("color: gray; font-size: 11px;");
            nameBox->addWidget(notesLbl);
        }
        row->addLayout(nameBox, 1);

        if (item.external) {
            auto* extLbl = new QLabel(runtimeName.isEmpty() ? "Not on Nexus" : "Proton runtime", rowFrame);
            extLbl->setStyleSheet("color: gray;");
            extLbl->setToolTip(runtimeName.isEmpty()
                ? QStringLiteral("This requirement isn't a Nexus mod for this game.")
                : QStringLiteral("A Windows runtime Proton provides in the game prefix; "
                                 "manage it with protontricks if needed."));
            row->addWidget(extLbl);
        } else {
            anyInstallable = true;
            auto* btn = new QPushButton("Install", rowFrame);
            const QString id = item.modId;
            const QString name = item.modName;
            connect(btn, &QPushButton::clicked, this, [this, btn, id, name] {
                emit installRequested(id, name);
                btn->setEnabled(false);
                btn->setText("Installing…");
            });
            m_buttons.insert(item.modId, btn);
            row->addWidget(btn);
        }

        layout->addWidget(rowFrame);
    }

    layout->addStretch(1);

    auto* btnRow = new QHBoxLayout();
    m_installAll = new QPushButton("Install all", this);
    m_installAll->setEnabled(anyInstallable);
    connect(m_installAll, &QPushButton::clicked, this, [this, missing] {
        for (const auto& item : missing) {
            if (item.external) continue;
            emit installRequested(item.modId, item.modName);
            markInstalling(item.modId);
        }
        m_installAll->setEnabled(false);
    });
    btnRow->addWidget(m_installAll);
    btnRow->addStretch(1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::accept);
    btnRow->addWidget(box);

    layout->addLayout(btnRow);
}

void RequirementsDialog::markInstalling(const QString& modId) {
    auto it = m_buttons.find(modId);
    if (it == m_buttons.end() || !it.value()) return;
    it.value()->setEnabled(false);
    it.value()->setText("Installing…");
}

void RequirementsDialog::markInstalled(const QString& modId) {
    auto it = m_buttons.find(modId);
    if (it == m_buttons.end() || !it.value()) return;
    QPushButton* btn = it.value();
    btn->setEnabled(false);
    btn->setText("Installed");
    btn->setStyleSheet("color: #7ec97e;"); // green: this requirement is now satisfied
}

void RequirementsDialog::markFailed(const QString& modId) {
    auto it = m_buttons.find(modId);
    if (it == m_buttons.end() || !it.value()) return;
    QPushButton* btn = it.value();
    btn->setEnabled(true);  // let the user try again
    btn->setText("Retry");
}

} // namespace solero
