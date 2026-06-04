#pragma once
#include <QWidget>
#include "core/Types.h"
class QLabel;
namespace solero {
class ToolPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolPanel(const Executable& exe, QWidget* parent = nullptr);
signals:
    void runRequested(const Executable& exe);
    void editRequested(const QString& id);
    void removeRequested(const QString& id);
private:
    Executable m_exe;
};
}
