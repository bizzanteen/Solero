#include "RequirementsDialog.h"
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

        // Name (+ optional notes as a dim secondary line).
        auto* nameBox = new QVBoxLayout();
        nameBox->setContentsMargins(0, 0, 0, 0);
        nameBox->setSpacing(0);
        auto* nameLbl = new QLabel(item.modName.isEmpty() ? item.modId : item.modName, rowFrame);
        nameBox->addWidget(nameLbl);
        if (!item.notes.isEmpty()) {
            auto* notesLbl = new QLabel(item.notes, rowFrame);
            notesLbl->setWordWrap(true);
            notesLbl->setStyleSheet("color: gray; font-size: 11px;");
            nameBox->addWidget(notesLbl);
        }
        row->addLayout(nameBox, 1);

        if (item.external) {
            auto* extLbl = new QLabel("Not on Nexus", rowFrame);
            extLbl->setStyleSheet("color: gray;");
            extLbl->setToolTip("This requirement isn't a Nexus mod for this game.");
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

} // namespace solero
