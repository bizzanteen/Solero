#pragma once
#include <QApplication>

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int& argc, char** argv);
    void setZoomFactor(double factor);
    double zoomFactor() const { return m_zoomFactor; }

private:
    double m_zoomFactor = 1.0;
    void applyZoom();
    void saveZoom();
    void loadZoom();
};
