#pragma once
#include <QWidget>
#include <QList>
#include <QHash>
#include "BethiniData.h"
#include "IniFile.h"
#include "core/Profile.h"

class QComboBox;
class QTabWidget;
class QPlainTextEdit;
class QLabel;

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
    void applyRecommendedTweaks();
    void onSave();
    void onSearch(const QString& filter);
    void onAdvancedFileChanged();
    void onAdvancedSave();

private:
    struct RowWidget {
        BethiniRow row;
        QWidget*   widget = nullptr;  // editor widget (combo/spin/check/edit)
        QWidget*   container = nullptr; // the labeled row container (for search hide)
    };

    void buildUI();
    void buildAdvancedTab(QTabWidget* tabs);
    void reloadRows();                       // re-read all row widgets from the INIs
    void showStatus(const QString& message);  // green feedback, auto-clears
    void loadAdvancedFile();
    QString iniPathFor(const QString& file) const;
    IniFile& iniFor(const QString& file) const;     // lazy-load into cache
    void saveAllInis();                             // flush dirty cached INIs
    QVariant readKey(const BethiniIniKey& k) const;
    void writeKey(const BethiniIniKey& k, const QVariant& v);

    void loadRow(RowWidget& rw);
    void saveRow(RowWidget& rw);
    bool rowDiffersFromIni(const RowWidget& rw) const;
    void applyPresetToRow(RowWidget& rw, const QString& presetName);

    // Dropdown helpers
    int matchChoiceFromIni(const RowWidget& rw) const;        // -> settingChoices index or -1
    void writeChoice(const RowWidget& rw, int choiceIndex);

    // Resolution dropdown (aspect-ratio filtered)
    void populateResolutions(const QString& aspect);

    Profile* m_profile = nullptr;
    QList<RowWidget> m_rows;

    mutable QHash<QString, IniFile> m_iniCache; // keyed by absolute INI path

    int        m_resRowIndex = -1;        // index into m_rows of the Resolution row
    QComboBox* m_resCombo = nullptr;      // the Resolution dropdown

    QComboBox*      m_advFileCombo = nullptr; // Advanced tab: which INI to edit
    QPlainTextEdit* m_advEdit = nullptr;      // Advanced tab: raw INI text

    QLabel* m_statusLabel = nullptr;          // green feedback line
};

} // namespace solero
