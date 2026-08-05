#pragma once
#include <QWidget>
#include "core/Profile.h"
#include "loot/LootUserlist.h"

class QTextEdit;

namespace solero {

class LootRulesEditor : public QWidget {
    Q_OBJECT
public:
    explicit LootRulesEditor(QWidget* parent = nullptr);
    void setProfile(Profile* profile);

    // Read `profile`'s userlist file, apply LootUserlist::addRule(), and atomically
    // write it back. Returns false + sets *err on an I/O failure. Creates the file
    // if none.
    static bool addPluginRule(Profile* profile, const QString& plugin,
                              LootRuleType kind, const QString& target,
                              QString* err = nullptr);

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
