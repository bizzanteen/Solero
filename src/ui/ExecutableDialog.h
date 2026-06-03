#pragma once
#include <QDialog>
#include "core/Types.h"
class QLineEdit; class QComboBox; class QCheckBox; class QPushButton;
namespace solero {
class ExecutableDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExecutableDialog(const Executable& exe = {}, QWidget* parent = nullptr);
    Executable result() const { return m_result; }
private:
    void browseForBinary();
    void onRuntimeChanged(int idx);
    void populateProtonVersions();
    Executable m_result;
    QLineEdit* m_nameEdit;
    QLineEdit* m_binaryEdit;
    QLineEdit* m_argsEdit;
    QLineEdit* m_workdirEdit;
    QComboBox* m_runtimeCombo;
    QComboBox* m_protonCombo;
    QLineEdit* m_prefixEdit;
    QCheckBox* m_deployCheck;
    QCheckBox* m_primaryCheck;
    QWidget*   m_protonGroup;
};
}
