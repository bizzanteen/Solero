#include "LootRulesEditor.h"
#include "YamlHighlighter.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFile>
#include <QMessageBox>
#include <QFont>

namespace solero {

LootRulesEditor::LootRulesEditor(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* snippetBar = new QHBoxLayout;
    snippetBar->addWidget(new QLabel("Insert:"));

    static const QList<QPair<QString,QString>> snippets = {
        {"Load After", "plugins:\n  - name: \"MyMod.esp\"\n    after:\n      - name: \"OtherMod.esp\"\n"},
        {"Incompatibility", "plugins:\n  - name: \"ModA.esp\"\n    inc:\n      - name: \"ModB.esp\"\n        display: \"ModA and ModB are incompatible\"\n"},
        {"Warning", "plugins:\n  - name: \"MyMod.esp\"\n    msg:\n      - type: warn\n        content: \"Requires Patch.esp\"\n"},
        {"Assign Group", "plugins:\n  - name: \"MyMod.esp\"\n    group: \"Worldspace settings\"\n"},
        {"Define Group", "groups:\n  - name: \"Worldspace settings\"\n    after:\n      - \"default\"\n"},
    };
    for (const auto& [label, text] : snippets) {
        auto* btn = new QPushButton(label, this);
        btn->setFixedHeight(22);
        connect(btn, &QPushButton::clicked, this, [this, t = text]{ insertSnippet(t); });
        snippetBar->addWidget(btn);
    }
    snippetBar->addStretch();

    auto* saveBtn = new QPushButton("Save", this);
    saveBtn->setFixedHeight(22);
    connect(saveBtn, &QPushButton::clicked, this, &LootRulesEditor::onSave);
    snippetBar->addWidget(saveBtn);
    layout->addLayout(snippetBar);

    m_editor = new QTextEdit(this);
    QFont font("Monospace");
    font.setStyleHint(QFont::TypeWriter);
    font.setPointSize(9);
    m_editor->setFont(font);
    new YamlHighlighter(m_editor->document());
    connect(m_editor, &QTextEdit::textChanged, this, [this]{ m_dirty = true; });
    layout->addWidget(m_editor);
}

void LootRulesEditor::setProfile(Profile* profile) {
    if (m_dirty && m_profile) {
        auto ret = QMessageBox::question(this, "Unsaved Changes",
            "Save LOOT rules before switching profile?",
            QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::Yes) onSave();
    }
    m_profile = profile;
    load();
}

void LootRulesEditor::load() {
    m_dirty = false;
    if (!m_profile) { m_editor->clear(); return; }
    QFile f(m_profile->lootUserlistPath());
    if (!f.open(QIODevice::ReadOnly)) {
        m_editor->clear();
        m_editor->setPlaceholderText(
            "# No userlist.yaml yet.\n# Use the snippet buttons above to add rules.\n");
        m_dirty = false;
        return;
    }
    m_editor->setPlainText(QString::fromUtf8(f.readAll()));
    m_dirty = false;
}

void LootRulesEditor::onSave() {
    if (!m_profile) return;
    QFile f(m_profile->lootUserlistPath());
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Save Failed", "Could not write userlist.yaml.");
        return;
    }
    f.write(m_editor->toPlainText().toUtf8());
    m_dirty = false;
}

void LootRulesEditor::insertSnippet(const QString& snippet) {
    m_editor->insertPlainText("\n" + snippet);
}

} // namespace solero
