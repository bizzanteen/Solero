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

namespace {

// Tiny, line-based YAML helpers for the additive per-plugin rule editor
// A full YAML parser is overkill (and we must not clobber the user's hand edits),
// so we splice into a canonical, indentation-stable shape:
//   plugins:
//     - name: "Plugin.esp"
//       after:
//         - "Other.esp"
//       group: "GroupName"

const QString kEntryIndent = QStringLiteral("  ");     // "  - name:"
const QString kKeyIndent   = QStringLiteral("    ");   // "    after:"
const QString kItemIndent  = QStringLiteral("      "); // "      - \"x\""

QString ruleKey(LootRulesEditor::LootRule k) {
    switch (k) {
        case LootRulesEditor::LootRule::LoadAfter:     return QStringLiteral("after");
        case LootRulesEditor::LootRule::Requires:      return QStringLiteral("req");
        case LootRulesEditor::LootRule::Incompatible:  return QStringLiteral("inc");
        case LootRulesEditor::LootRule::SetGroup:      return QStringLiteral("group");
    }
    return QStringLiteral("after");
}

QString yamlQuote(const QString& s) {
    QString e = s;
    e.replace('\\', QStringLiteral("\\\\"));
    e.replace('"', QStringLiteral("\\\""));
    return '"' + e + '"';
}

// Strip surrounding single/double quotes (and any "\\\"" escaping) from a scalar.
QString yamlUnquote(const QString& raw) {
    QString s = raw.trimmed();
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        s = s.mid(1, s.size() - 2);
        s.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        s.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    }
    return s;
}

int leadingSpaces(const QString& l) {
    int i = 0;
    while (i < l.size() && l[i] == ' ') ++i;
    return i;
}

// True if `line` is an entry line "- name: <plugin>" (any indent/quote style).
bool isNameLine(const QString& line, const QString& plugin) {
    const QString s = line.trimmed();
    if (!s.startsWith(QStringLiteral("- name:"))) return false;
    const QString val = s.mid(QStringLiteral("- name:").size());
    return yamlUnquote(val).compare(plugin, Qt::CaseInsensitive) == 0;
}

QStringList freshKeyBlock(LootRulesEditor::LootRule kind, const QString& target) {
    if (kind == LootRulesEditor::LootRule::SetGroup)
        return { kKeyIndent + QStringLiteral("group: ") + yamlQuote(target) };
    return { kKeyIndent + ruleKey(kind) + QStringLiteral(":"),
             kItemIndent + QStringLiteral("- ") + yamlQuote(target) };
}

QStringList freshEntry(const QString& plugin, LootRulesEditor::LootRule kind,
                       const QString& target) {
    QStringList out;
    out << kEntryIndent + QStringLiteral("- name: ") + yamlQuote(plugin);
    out += freshKeyBlock(kind, target);
    return out;
}

} // namespace

QString LootRulesEditor::appendPluginRule(const QString& yaml, const QString& plugin,
                                          LootRule kind, const QString& target) {
    QStringList lines = yaml.isEmpty() ? QStringList() : yaml.split('\n');

    // Locate (or create) the top-level `plugins:` block.
    int hdr = -1;
    for (int i = 0; i < lines.size(); ++i)
        if (lines[i].startsWith(QStringLiteral("plugins:"))) { hdr = i; break; }

    if (hdr < 0) {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty()) lines << QString();
        lines << QStringLiteral("plugins:");
        lines += freshEntry(plugin, kind, target);
        return lines.join('\n');
    }

    // Extent of the plugins block: until the next column-0 (top-level) key or EOF.
    int blockEnd = lines.size();
    for (int i = hdr + 1; i < lines.size(); ++i) {
        const QString& l = lines[i];
        if (!l.isEmpty() && !l[0].isSpace()) { blockEnd = i; break; }
    }

    // Find this plugin's entry within the block.
    int entryLine = -1;
    for (int i = hdr + 1; i < blockEnd; ++i)
        if (isNameLine(lines[i], plugin)) { entryLine = i; break; }

    if (entryLine < 0) {
        const QStringList entry = freshEntry(plugin, kind, target);
        for (int j = 0; j < entry.size(); ++j) lines.insert(blockEnd + j, entry[j]);
        return lines.join('\n');
    }

    // Entry's own line range: from entryLine to the next same-indent "- " or blockEnd.
    const int entryIndent = leadingSpaces(lines[entryLine]);
    int entryEnd = blockEnd;
    for (int i = entryLine + 1; i < blockEnd; ++i) {
        const QString& l = lines[i];
        if (l.trimmed().startsWith(QStringLiteral("- ")) && leadingSpaces(l) <= entryIndent) {
            entryEnd = i; break;
        }
    }

    if (kind == LootRule::SetGroup) {
        // Replace an existing scalar group, else insert one right after the name.
        for (int i = entryLine + 1; i < entryEnd; ++i) {
            if (lines[i].trimmed().startsWith(QStringLiteral("group:"))) {
                lines[i] = kKeyIndent + QStringLiteral("group: ") + yamlQuote(target);
                return lines.join('\n');
            }
        }
        lines.insert(entryLine + 1, kKeyIndent + QStringLiteral("group: ") + yamlQuote(target));
        return lines.join('\n');
    }

    // List key (after / req / inc): find the key line within the entry.
    const QString key = ruleKey(kind);
    int keyLine = -1;
    for (int i = entryLine + 1; i < entryEnd; ++i)
        if (lines[i].trimmed() == key + QStringLiteral(":")
            || lines[i].trimmed().startsWith(key + QStringLiteral(":"))) {
            // Guard against a false match on a longer key sharing the prefix.
            const QString t = lines[i].trimmed();
            if (t == key + QStringLiteral(":")) { keyLine = i; break; }
        }

    if (keyLine < 0) {
        // No such key yet - add the key + first item after the name line.
        const QStringList block = freshKeyBlock(kind, target);
        for (int j = 0; j < block.size(); ++j) lines.insert(entryLine + 1 + j, block[j]);
        return lines.join('\n');
    }

    // Append to the existing list; skip if the target is already present.
    int listEnd = entryEnd;
    const int keyIndent = leadingSpaces(lines[keyLine]);
    for (int i = keyLine + 1; i < entryEnd; ++i) {
        const QString s = lines[i].trimmed();
        if (s.startsWith(QStringLiteral("- "))) {
            if (yamlUnquote(s.mid(2)).compare(target, Qt::CaseInsensitive) == 0)
                return yaml; // already listed - no-op
            continue;
        }
        // A line that isn't a list item and is indented no deeper than the key ends it.
        if (!s.isEmpty() && leadingSpaces(lines[i]) <= keyIndent) { listEnd = i; break; }
    }
    lines.insert(listEnd, kItemIndent + QStringLiteral("- ") + yamlQuote(target));
    return lines.join('\n');
}

bool LootRulesEditor::addPluginRule(Profile* profile, const QString& plugin,
                                    LootRule kind, const QString& target, QString* err) {
    if (!profile) { if (err) *err = QStringLiteral("No active profile."); return false; }
    const QString path = profile->lootUserlistPath();
    QString existing;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) { existing = QString::fromUtf8(f.readAll()); f.close(); }
    const QString updated = appendPluginRule(existing, plugin, kind, target);
    if (!atomicWrite(path, updated.toUtf8())) {
        if (err) *err = QStringLiteral("Could not write the LOOT userlist file.");
        return false;
    }
    return true;
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
