#pragma once
#include <QDialog>
#include "core/Types.h"
class QLineEdit; class QPushButton; class QListWidget; class QCheckBox;
namespace solero {
class SeparatorDialog : public QDialog {
    Q_OBJECT
public:
    explicit SeparatorDialog(const ModEntry& sep, QWidget* parent = nullptr);
    ModEntry result() const { return m_result; }
private:
    void pickColor();
    ModEntry m_result;
    QLineEdit* m_nameEdit;
    QListWidget* m_iconList;
    QCheckBox* m_colorIconCheck;
    QPushButton* m_colorBtn;
};
}
