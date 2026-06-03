#pragma once
#include <QTabWidget>
namespace solero {
class BottomPanel : public QTabWidget {
    Q_OBJECT
public:
    explicit BottomPanel(QWidget* parent = nullptr);
};
}
