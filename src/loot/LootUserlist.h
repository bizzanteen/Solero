#pragma once
#include <QList>
#include <QString>
#include <QStringList>

namespace solero {

// The rule kinds a profile's loot-userlist.yaml can carry per plugin. The names
// match LOOT's own metadata keys, which is also the vocabulary the IPC/MCP layer
// speaks.
enum class LootRuleType { After, Req, Inc, Group };

struct LootPluginRules {
    QString     name;
    QStringList after;
    QStringList req;
    QStringList inc;
    QString     group;  // empty when the plugin is in no explicit group
};

// Reading and editing loot-userlist.yaml without a YAML library.
//
// These files are hand-editable, so every operation here is additive and
// line-based: it splices into the canonical shape Solero writes and otherwise
// leaves the user's formatting, comments and ordering alone. A full parse +
// re-emit would silently reformat (and drop comments from) files people
// maintain by hand.
//
// Everything is pure - no I/O, no libloot. Group *existence* has to be checked
// against libloot (the masterlist defines most groups), so validateGroupName()
// takes the list of valid names rather than looking them up.
namespace LootUserlist {

QString ruleTypeName(LootRuleType kind);
bool    ruleTypeFromName(const QString& name, LootRuleType* out);

// Rules currently in the file, in document order. Handles both list-item forms
// LOOT accepts: a bare scalar and a `- name:` mapping.
QList<LootPluginRules> parsePlugins(const QString& yaml);

// Group names defined by the file's own top-level `groups:` block. This is only
// part of the picture - most groups come from the masterlist.
QStringList parseGroups(const QString& yaml);

// Idempotent: adding a rule that is already present returns `yaml` unchanged.
// A second Group rule replaces the first, since a plugin has exactly one group.
QString addRule(const QString& yaml, const QString& plugin, LootRuleType kind,
                const QString& target);

// Removes one target from one rule. Sets *removed to false and returns `yaml`
// unchanged when the rule isn't there. Drops the rule key once its last target
// goes (an empty `after:` is a null value LOOT complains about), and then the
// plugin entry and the `plugins:` header once those are empty too.
QString removeRule(const QString& yaml, const QString& plugin, LootRuleType kind,
                   const QString& target, bool* removed = nullptr);

// Empty string when `name` is a usable group, otherwise a message explaining
// why not. Worth being fussy about: LOOT group names are case-sensitive, and a
// reference to an undefined group aborts the *entire* sort rather than skipping
// the one rule.
QString validateGroupName(const QString& name, const QStringList& available);

} // namespace LootUserlist
} // namespace solero
