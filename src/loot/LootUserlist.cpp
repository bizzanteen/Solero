#include "LootUserlist.h"

namespace solero {
namespace {

// The shape we splice into:
//   plugins:
//     - name: "Plugin.esp"
//       after:
//         - "Other.esp"
//       group: "GroupName"
// The absolute indents are taken from the file rather than fixed, because a
// document whose whole root mapping is indented is still valid YAML and LOOT
// reads it fine - so real userlists in the wild start deeper than column 0.
struct Indents {
    QString entry; // "  - name:"
    QString key;   // "    after:"
    QString item;  // "      - \"x\""
};

Indents indentsFor(int entryIndent) {
    return { QString(entryIndent, ' '),
             QString(entryIndent + 2, ' '),   // keys line up under `name`
             QString(entryIndent + 4, ' ') };
}

int leadingSpaces(const QString& l) {
    int i = 0;
    while (i < l.size() && l[i] == ' ') ++i;
    return i;
}

QString yamlQuote(const QString& s) {
    QString e = s;
    e.replace('\\', QStringLiteral("\\\\"));
    e.replace('"', QStringLiteral("\\\""));
    return '"' + e + '"';
}

// Strip surrounding single/double quotes (and any escaping) from a scalar.
QString yamlUnquote(const QString& raw) {
    QString s = raw.trimmed();
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        s = s.mid(1, s.size() - 2);
        s.replace(QStringLiteral("\\\""), QStringLiteral("\""));
        s.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    }
    return s;
}

// The root-level block named `key`: its header line, the line after its last,
// and the header's own indent. hdr is -1 when there is no such block.
struct Block {
    int hdr    = -1;
    int end    = -1;
    int indent = 0;
};

Block blockRange(const QStringList& lines, const QString& key) {
    const QString header = key + QStringLiteral(":");
    Block b;
    for (int i = 0; i < lines.size(); ++i) {
        const QString s = lines[i].trimmed();
        if (!s.startsWith(header) || s.startsWith(QStringLiteral("- "))) continue;
        b.hdr    = i;
        b.indent = leadingSpaces(lines[i]);
        break;
    }
    if (b.hdr < 0) return b;
    b.end = lines.size();
    for (int i = b.hdr + 1; i < lines.size(); ++i) {
        if (lines[i].trimmed().isEmpty()) continue;
        if (leadingSpaces(lines[i]) <= b.indent) { b.end = i; break; }
    }
    return b;
}

// Indent of the block's own entries. List items nested under a rule key are
// indented deeper, which is what tells "  - name: X" (an entry) apart from
// "      - name: X" (an `after:` target written in mapping form). -1 when the
// block holds no entries.
int entryIndent(const QStringList& lines, int hdr, int end) {
    for (int i = hdr + 1; i < end; ++i)
        if (lines[i].trimmed().startsWith(QStringLiteral("- ")))
            return leadingSpaces(lines[i]);
    return -1;
}

bool isEntryStart(const QStringList& lines, int i, int indent) {
    return leadingSpaces(lines[i]) == indent
        && lines[i].trimmed().startsWith(QStringLiteral("- "));
}

// True if `line` is the entry line "- name: <plugin>" for this plugin.
bool isNameLine(const QString& line, const QString& plugin) {
    const QString s = line.trimmed();
    if (!s.startsWith(QStringLiteral("- name:"))) return false;
    return yamlUnquote(s.mid(7)).compare(plugin, Qt::CaseInsensitive) == 0;
}

// A list item's value, whether written as `- "X"` or `- name: "X"`.
QString listItemValue(const QString& trimmedLine) {
    QString item = trimmedLine.mid(2).trimmed();
    if (item.startsWith(QStringLiteral("name:"))) item = item.mid(5);
    return yamlUnquote(item);
}

QStringList freshKeyBlock(LootRuleType kind, const QString& target, const Indents& ind) {
    if (kind == LootRuleType::Group)
        return { ind.key + QStringLiteral("group: ") + yamlQuote(target) };
    return { ind.key  + LootUserlist::ruleTypeName(kind) + QStringLiteral(":"),
             ind.item + QStringLiteral("- ") + yamlQuote(target) };
}

QStringList freshEntry(const QString& plugin, LootRuleType kind, const QString& target,
                       const Indents& ind) {
    QStringList out;
    out << ind.entry + QStringLiteral("- name: ") + yamlQuote(plugin);
    out += freshKeyBlock(kind, target, ind);
    return out;
}

// Drop plugin entries left with nothing but a name, then a `plugins:` header
// left with no entries. Runs after a removal so the file doesn't accumulate
// husks.
QString pruneEmpty(const QStringList& in) {
    QStringList lines = in;
    auto range = blockRange(lines, QStringLiteral("plugins"));
    if (range.hdr < 0) return lines.join('\n');

    const int ind = entryIndent(lines, range.hdr, range.end);
    if (ind >= 0) {
        QList<int> starts;
        for (int i = range.hdr + 1; i < range.end; ++i)
            if (isEntryStart(lines, i, ind)) starts << i;
        for (int k = starts.size() - 1; k >= 0; --k) {
            const int start = starts[k];
            const int stop  = (k + 1 < starts.size()) ? starts[k + 1] : range.end;
            bool hasRules = false;
            for (int i = start + 1; i < stop; ++i)
                if (!lines[i].trimmed().isEmpty()) { hasRules = true; break; }
            if (!hasRules)
                for (int i = stop - 1; i >= start; --i) lines.removeAt(i);
        }
    }

    range = blockRange(lines, QStringLiteral("plugins"));
    if (range.hdr >= 0 && entryIndent(lines, range.hdr, range.end) < 0)
        lines.removeAt(range.hdr);
    return lines.join('\n');
}

} // namespace

namespace LootUserlist {

QString ruleTypeName(LootRuleType kind) {
    switch (kind) {
        case LootRuleType::After: return QStringLiteral("after");
        case LootRuleType::Req:   return QStringLiteral("req");
        case LootRuleType::Inc:   return QStringLiteral("inc");
        case LootRuleType::Group: return QStringLiteral("group");
    }
    return QStringLiteral("after");
}

bool ruleTypeFromName(const QString& name, LootRuleType* out) {
    static const QList<LootRuleType> kinds = {
        LootRuleType::After, LootRuleType::Req, LootRuleType::Inc, LootRuleType::Group };
    for (LootRuleType k : kinds) {
        if (name == ruleTypeName(k)) { if (out) *out = k; return true; }
    }
    return false;
}

QList<LootPluginRules> parsePlugins(const QString& yaml) {
    QList<LootPluginRules> out;
    const QStringList lines = yaml.split('\n');
    const auto range = blockRange(lines, QStringLiteral("plugins"));
    if (range.hdr < 0) return out;
    const int ind = entryIndent(lines, range.hdr, range.end);
    if (ind < 0) return out;

    QString listKey; // the rule key whose items we are currently collecting
    for (int i = range.hdr + 1; i < range.end; ++i) {
        const QString s = lines[i].trimmed();
        if (s.isEmpty() || s.startsWith('#')) continue;

        if (isEntryStart(lines, i, ind)) {
            out.append(LootPluginRules{});
            listKey.clear();
            if (s.startsWith(QStringLiteral("- name:")))
                out.last().name = yamlUnquote(s.mid(7));
            continue;
        }
        if (out.isEmpty()) continue;
        LootPluginRules& cur = out.last();

        if (s.startsWith(QStringLiteral("- "))) {
            const QString item = listItemValue(s);
            if (item.isEmpty()) continue;
            if      (listKey == QStringLiteral("after")) cur.after << item;
            else if (listKey == QStringLiteral("req"))   cur.req   << item;
            else if (listKey == QStringLiteral("inc"))   cur.inc   << item;
            continue;
        }

        const int colon = s.indexOf(':');
        if (colon < 0) continue;
        const QString key   = s.left(colon).trimmed();
        const QString value = yamlUnquote(s.mid(colon + 1));
        if (key == QStringLiteral("group")) {
            cur.group = value;
            listKey.clear();
        } else if (value.isEmpty()
                   && (key == QStringLiteral("after") || key == QStringLiteral("req")
                       || key == QStringLiteral("inc"))) {
            listKey = key;
        } else {
            listKey.clear(); // some other key (msg, url, tag, ...) - its items aren't ours
        }
    }
    return out;
}

QStringList parseGroups(const QString& yaml) {
    QStringList out;
    const QStringList lines = yaml.split('\n');
    const auto range = blockRange(lines, QStringLiteral("groups"));
    if (range.hdr < 0) return out;
    const int ind = entryIndent(lines, range.hdr, range.end);
    if (ind < 0) return out;
    for (int i = range.hdr + 1; i < range.end; ++i) {
        if (!isEntryStart(lines, i, ind)) continue;
        const QString s = lines[i].trimmed();
        if (!s.startsWith(QStringLiteral("- name:"))) continue;
        const QString name = yamlUnquote(s.mid(7));
        if (!name.isEmpty()) out << name;
    }
    return out;
}

QString addRule(const QString& yaml, const QString& plugin, LootRuleType kind,
                const QString& target) {
    QStringList lines = yaml.isEmpty() ? QStringList() : yaml.split('\n');

    auto range = blockRange(lines, QStringLiteral("plugins"));
    if (range.hdr < 0) {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty()) lines << QString();
        lines << QStringLiteral("plugins:");
        lines += freshEntry(plugin, kind, target, indentsFor(2));
        return lines.join('\n');
    }
    const int hdr = range.hdr, blockEnd = range.end;
    // Follow the block's own indentation; an empty block sets the convention.
    int ind = entryIndent(lines, hdr, blockEnd);
    const Indents indents = indentsFor(ind >= 0 ? ind : range.indent + 2);

    int entryLine = -1;
    if (ind >= 0) {
        for (int i = hdr + 1; i < blockEnd; ++i)
            if (isEntryStart(lines, i, ind) && isNameLine(lines[i], plugin)) { entryLine = i; break; }
    }
    if (entryLine < 0) {
        // Append after the block's last rule, not after any trailing blank lines.
        int at = blockEnd;
        while (at > hdr + 1 && lines[at - 1].trimmed().isEmpty()) --at;
        const QStringList entry = freshEntry(plugin, kind, target, indents);
        for (int j = 0; j < entry.size(); ++j) lines.insert(at + j, entry[j]);
        return lines.join('\n');
    }

    // This entry's own lines: up to the next entry at the same indent, or the
    // end of the block.
    int entryEnd = blockEnd;
    for (int i = entryLine + 1; i < blockEnd; ++i)
        if (isEntryStart(lines, i, ind)) { entryEnd = i; break; }

    if (kind == LootRuleType::Group) {
        // One group per plugin: replace an existing assignment rather than
        // stacking a second one.
        for (int i = entryLine + 1; i < entryEnd; ++i) {
            if (lines[i].trimmed().startsWith(QStringLiteral("group:"))) {
                lines[i] = indents.key + QStringLiteral("group: ") + yamlQuote(target);
                return lines.join('\n');
            }
        }
        lines.insert(entryLine + 1, indents.key + QStringLiteral("group: ") + yamlQuote(target));
        return lines.join('\n');
    }

    const QString key = ruleTypeName(kind);
    int keyLine = -1;
    for (int i = entryLine + 1; i < entryEnd; ++i)
        if (lines[i].trimmed() == key + QStringLiteral(":")) { keyLine = i; break; }

    if (keyLine < 0) {
        const QStringList block = freshKeyBlock(kind, target, indents);
        for (int j = 0; j < block.size(); ++j) lines.insert(entryLine + 1 + j, block[j]);
        return lines.join('\n');
    }

    int listEnd = entryEnd;
    const int keyIndent = leadingSpaces(lines[keyLine]);
    for (int i = keyLine + 1; i < entryEnd; ++i) {
        const QString s = lines[i].trimmed();
        if (s.startsWith(QStringLiteral("- "))) {
            if (listItemValue(s).compare(target, Qt::CaseInsensitive) == 0)
                return yaml; // already listed - adding it again would duplicate it
            continue;
        }
        if (!s.isEmpty() && leadingSpaces(lines[i]) <= keyIndent) { listEnd = i; break; }
    }
    lines.insert(listEnd, indents.item + QStringLiteral("- ") + yamlQuote(target));
    return lines.join('\n');
}

QString removeRule(const QString& yaml, const QString& plugin, LootRuleType kind,
                   const QString& target, bool* removed) {
    if (removed) *removed = false;
    QStringList lines = yaml.split('\n');
    const auto range = blockRange(lines, QStringLiteral("plugins"));
    if (range.hdr < 0) return yaml;
    const int ind = entryIndent(lines, range.hdr, range.end);
    if (ind < 0) return yaml;

    int entryLine = -1;
    for (int i = range.hdr + 1; i < range.end; ++i)
        if (isEntryStart(lines, i, ind) && isNameLine(lines[i], plugin)) { entryLine = i; break; }
    if (entryLine < 0) return yaml;

    int entryEnd = range.end;
    for (int i = entryLine + 1; i < range.end; ++i)
        if (isEntryStart(lines, i, ind)) { entryEnd = i; break; }

    if (kind == LootRuleType::Group) {
        for (int i = entryLine + 1; i < entryEnd; ++i) {
            const QString s = lines[i].trimmed();
            if (!s.startsWith(QStringLiteral("group:"))) continue;
            // Group names are case-sensitive, so this comparison is too.
            if (yamlUnquote(s.mid(6)) != target) return yaml;
            lines.removeAt(i);
            if (removed) *removed = true;
            return pruneEmpty(lines);
        }
        return yaml;
    }

    const QString key = ruleTypeName(kind);
    int keyLine = -1;
    for (int i = entryLine + 1; i < entryEnd; ++i)
        if (lines[i].trimmed() == key + QStringLiteral(":")) { keyLine = i; break; }
    if (keyLine < 0) return yaml;

    const int keyIndent = leadingSpaces(lines[keyLine]);
    int hit = -1, items = 0;
    for (int i = keyLine + 1; i < entryEnd; ++i) {
        const QString s = lines[i].trimmed();
        if (s.startsWith(QStringLiteral("- "))) {
            ++items;
            if (hit < 0 && listItemValue(s).compare(target, Qt::CaseInsensitive) == 0) hit = i;
            continue;
        }
        if (!s.isEmpty() && leadingSpaces(lines[i]) <= keyIndent) break;
    }
    if (hit < 0) return yaml;

    lines.removeAt(hit);
    if (items == 1) lines.removeAt(keyLine); // an empty `after:` is a null LOOT rejects
    if (removed) *removed = true;
    return pruneEmpty(lines);
}

QString validateGroupName(const QString& name, const QStringList& available) {
    if (name.trimmed().isEmpty())
        return QStringLiteral("A group name is required.");
    if (available.isEmpty())
        return QStringLiteral("The list of LOOT groups could not be read, so \"%1\" can't be "
                              "verified. Assigning an undefined group aborts the whole sort, "
                              "so this rule was not written.").arg(name);
    if (available.contains(name)) return QString();

    for (const QString& g : available) {
        if (g.compare(name, Qt::CaseInsensitive) != 0) continue;
        return QStringLiteral("LOOT group names are case-sensitive. There is no group \"%1\", "
                              "but there is \"%2\" - use that exact spelling.").arg(name, g);
    }
    QStringList sorted = available;
    sorted.sort(Qt::CaseInsensitive);
    return QStringLiteral("There is no LOOT group named \"%1\". Defined groups are: %2.")
               .arg(name, sorted.join(QStringLiteral(", ")));
}

} // namespace LootUserlist
} // namespace solero
