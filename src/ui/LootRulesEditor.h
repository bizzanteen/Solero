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
