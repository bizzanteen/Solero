#include "SetupWizard.h"
#include "SetupPanel.h"
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QUrl>
#include "../nexus/NexusApi.h"

namespace solero {

SetupWizard::SetupWizard(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Welcome to Solero - Setup");
    setMinimumWidth(560);
    setModal(true);

    auto* layout = new QVBoxLayout(this);

    m_panel = new SetupPanel(this);
    layout->addWidget(m_panel);

    auto* nexusGroup = new QGroupBox("Nexus account (optional)", this);
    auto* nexusV = new QVBoxLayout(nexusGroup);
    auto* nexusHelp = new QLabel(
        "Paste your Nexus Personal API Key to enable in-app downloads and updates. "
        "You can skip this and add it later in Settings \xe2\x86\x92 Nexus Account.",
        nexusGroup);
    nexusHelp->setWordWrap(true);
    nexusV->addWidget(nexusHelp);

    m_apiKeyEdit = new QLineEdit(nexusGroup);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText("Paste your Nexus API key\xe2\x80\xa6 (optional)");
    nexusV->addWidget(m_apiKeyEdit);

    auto* keyBtnRow = new QHBoxLayout;
    auto* getKeyBtn = new QPushButton("Get API Key", nexusGroup);
    auto* pasteBtn  = new QPushButton("Paste from clipboard", nexusGroup);
    keyBtnRow->addWidget(getKeyBtn);
    keyBtnRow->addWidget(pasteBtn);
    keyBtnRow->addStretch();
    nexusV->addLayout(keyBtnRow);
    layout->addWidget(nexusGroup);

    connect(getKeyBtn, &QPushButton::clicked, this, []{
        QDesktopServices::openUrl(QUrl("https://www.nexusmods.com/users/myaccount?tab=api"));
    });
    connect(pasteBtn, &QPushButton::clicked, this, [this]{
        m_apiKeyEdit->setText(QGuiApplication::clipboard()->text().trimmed());
    });

    auto* btns = new QDialogButtonBox(this);
    m_acceptBtn = btns->addButton("Set Up Solero", QDialogButtonBox::AcceptRole);
    btns->addButton(QDialogButtonBox::Cancel);
    layout->addWidget(btns);

    m_acceptBtn->setEnabled(m_panel->isValid());
    connect(m_panel, &SetupPanel::validityChanged, m_acceptBtn, &QPushButton::setEnabled);
    connect(btns, &QDialogButtonBox::accepted, this, [this]{
        m_panel->save();
        const QString key = m_apiKeyEdit->text().trimmed();
        if (!key.isEmpty()) {
            const auto info = solero::NexusApi::validateUser(key);
            if (info.ok) {
                solero::NexusApi::setApiKey(key);
            } else {
                // Don't block setup on a bad key - they can fix it in Settings.
                QMessageBox::warning(this, "Nexus account",
                    "That API key didn't validate, so it wasn't saved. You can add it "
                    "later in Settings \xe2\x86\x92 Nexus Account.");
            }
        }
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace solero
