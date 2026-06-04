#pragma once
#include <QDialog>
#include <QPair>
#include <QList>
#include "core/Types.h"
class QLineEdit; class QComboBox; class QCheckBox; class QPushButton;
namespace solero {
class ExecutableDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExecutableDialog(const Executable& exe = {}, QWidget* parent = nullptr);
    Executable result() const { return m_result; }
    void setOutputModChoices(const QList<QPair<QString,QString>>& idName, const QString& currentId);
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
    QComboBox* m_outputCombo;
    QLineEdit* m_prefixEdit;
    QCheckBox* m_deployCheck;
    QCheckBox* m_primaryCheck;
    QWidget*   m_protonGroup;
};
}
