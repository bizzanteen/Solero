#pragma once
#include <QWidget>
#include "core/Profile.h"

class QTextEdit;

namespace solero {

class LootRulesEditor : public QWidget {
    Q_OBJECT
public:
    explicit LootRulesEditor(QWidget* parent = nullptr);
    void setProfile(Profile* profile);

    // per-plugin LOOT rule kinds surfaced as plugin-list context actions.
    enum class LootRule { LoadAfter, Requires, Incompatible, SetGroup };

    // Return `yaml` with a rule for `plugin` added. Additive: it reuses an existing
    // top-level `plugins:` block and an existing entry for `plugin` (creating either
    // if absent), appends the rule under the right key (after / req / inc / group),
    // and never removes or reorders existing rules. Duplicate list items are skipped;
    // SetGroup replaces the plugin's scalar group value. Pure - no I/O.
    static QString appendPluginRule(const QString& yaml, const QString& plugin,
                                    LootRule kind, const QString& target);

    // Read `profile`'s userlist file, apply appendPluginRule(), and atomically write
    // it back. Returns false + sets *err on an I/O failure. Creates the file if none.
    static bool addPluginRule(Profile* profile, const QString& plugin,
                              LootRule kind, const QString& target, QString* err = nullptr);

private slots:
    void onSave();
    void insertSnippet(const QString& snippet);

private:
    QTextEdit*  m_editor;
    Profile*    m_profile = nullptr;
    bool        m_dirty   = false;
    void load();
};

} // namespace solero
