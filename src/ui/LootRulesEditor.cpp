#include "LootRulesEditor.h"
#include "YamlHighlighter.h"
#include "core/FileUtil.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFile>
#include <QMessageBox>
#include <QFont>
#include <QSignalBlocker>
#include <QStringList>

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
        QSignalBlocker block(m_editor);
        m_editor->clear();
        m_editor->setPlaceholderText(
            "# No userlist.yaml yet.\n# Use the snippet buttons above to add rules.\n");
        m_dirty = false;
        return;
    }
    // Block textChanged so loading a profile doesn't mark it dirty (which would
    // trigger spurious "unsaved changes" prompts on the next profile switch).
    {
        QSignalBlocker block(m_editor);
        m_editor->setPlainText(QString::fromUtf8(f.readAll()));
    }
    m_dirty = false;
}

void LootRulesEditor::onSave() {
    if (!m_profile) return;

    const QString text = m_editor->toPlainText();

    // Basic sanity check: LOOT rejects a file with duplicate top-level keys, so
    // warn if there's more than one top-level `plugins:` or `groups:` line.
    int pluginsCount = 0, groupsCount = 0;
    for (const QString& line : text.split('\n')) {
        if (line.startsWith("plugins:")) ++pluginsCount;
        else if (line.startsWith("groups:")) ++groupsCount;
    }
    if (pluginsCount > 1 || groupsCount > 1) {
        auto ret = QMessageBox::warning(this, "Possibly Invalid Rules",
            QString("This file has duplicate top-level keys "
                    "(%1 \"plugins:\", %2 \"groups:\"). LOOT may reject it.\n\n"
                    "Save anyway?").arg(pluginsCount).arg(groupsCount),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
    }

    if (!atomicWrite(m_profile->lootUserlistPath(), text.toUtf8())) {
        QMessageBox::warning(this, "Save Failed", "Could not save the LOOT rules file. Make sure the profile folder is writable and try again.");
        return;
    }
    m_dirty = false;
}

void LootRulesEditor::insertSnippet(const QString& snippet) {
    // Each snippet is a full top-level block (e.g. starts with "plugins:" or
    // "groups:"). If the document already has that top-level key, splice only
    // the snippet's indented list items in after the existing header - inserting
    // a second top-level key would produce a YAML file LOOT rejects.
    const QStringList snippetLines = snippet.split('\n');
    QString topKey;
    if (!snippetLines.isEmpty()) {
        const QString& first = snippetLines.first();
        if (first.startsWith("plugins:")) topKey = "plugins:";
        else if (first.startsWith("groups:")) topKey = "groups:";
    }

    const QString doc = m_editor->toPlainText();
    const QStringList docLines = doc.split('\n');

    // Find an existing top-level header line (starts at column 0) for this key.
    int headerIdx = -1;
    if (!topKey.isEmpty()) {
        for (int i = 0; i < docLines.size(); ++i) {
            if (docLines.at(i).startsWith(topKey)) { headerIdx = i; break; }
        }
    }

    if (headerIdx < 0) {
        // No existing block - insert the whole snippet.
        m_editor->insertPlainText("\n" + snippet);
        return;
    }

    // Strip the snippet's own top-level header, keep the indented list items.
    QStringList items;
    for (int i = 1; i < snippetLines.size(); ++i) {
        const QString& l = snippetLines.at(i);
        if (l.isEmpty()) continue;       // drop the trailing blank line
        items << l;
    }
    if (items.isEmpty()) return;

    // Rebuild the document with the items spliced in right after the header.
    QStringList out = docLines;
    out.insert(headerIdx + 1, items.join('\n'));
    m_editor->setPlainText(out.join('\n'));
    m_dirty = true;
}

} // namespace solero
