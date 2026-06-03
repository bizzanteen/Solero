#pragma once
#include <QWidget>
#include <QHash>
#include "core/Profile.h"
#include "IniKnowledgeBase.h"

class QScrollArea;
class QLineEdit;
class QFormLayout;

namespace solero {

class IniEditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit IniEditorPanel(QWidget* parent = nullptr);
    void setProfile(Profile* profile);

private slots:
    void applyPreset(const QString& presetName);
    void onSave();

private:
    void buildUI();
    void loadValuesFromProfile();
    QString iniPathFor(const IniSetting& s) const;
    QVariant readIniValue(const IniSetting& s) const;
    void writeIniValue(const IniSetting& s, const QVariant& value);

    Profile*      m_profile = nullptr;
    QScrollArea*  m_scroll = nullptr;
    QLineEdit*    m_search = nullptr;
    QFormLayout*  m_form = nullptr;
    QHash<QString, QWidget*> m_widgets;       // "Section::Key" -> editor widget
    QHash<QString, QWidget*> m_rowLabels;     // "Section::Key" -> its row label (for search hide)
};

} // namespace solero
