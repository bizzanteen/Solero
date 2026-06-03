#pragma once
#include <QWidget>
#include <QList>
#include "BethiniData.h"
#include "core/Profile.h"

class QComboBox;

namespace solero {

// A faithful mirror of BethINI Pie: preset buttons + tabbed groups of labeled
// rows, reading/writing the active profile's INI files.
class BethiniWindow : public QWidget {
    Q_OBJECT
public:
    explicit BethiniWindow(QWidget* parent = nullptr);
    void setProfile(Profile* profile);

private slots:
    void applyPreset(const QString& presetName);
    void onSave();
    void onSearch(const QString& filter);

private:
    struct RowWidget {
        BethiniRow row;
        QWidget*   widget = nullptr;  // editor widget (combo/spin/check/edit)
        QWidget*   container = nullptr; // the labeled row container (for search hide)
    };

    void buildUI();
    QString iniPathFor(const QString& file) const;
    QVariant readKey(const BethiniIniKey& k) const;
    void writeKey(const BethiniIniKey& k, const QVariant& v);

    void loadRow(RowWidget& rw);
    void saveRow(RowWidget& rw);
    void applyPresetToRow(RowWidget& rw, const QString& presetName);

    // Dropdown helpers
    int matchChoiceFromIni(const RowWidget& rw) const;        // -> settingChoices index or -1
    void writeChoice(const RowWidget& rw, int choiceIndex);

    // Resolution dropdown (aspect-ratio filtered)
    void populateResolutions(const QString& aspect);

    Profile* m_profile = nullptr;
    QList<RowWidget> m_rows;

    int        m_resRowIndex = -1;        // index into m_rows of the Resolution row
    QComboBox* m_resCombo = nullptr;      // the Resolution dropdown
};

} // namespace solero
