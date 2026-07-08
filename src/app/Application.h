#pragma once
#include <QApplication>

class Application : public QApplication {
    Q_OBJECT
public:
    Application(int& argc, char** argv);
    void setZoomFactor(double factor);
    double zoomFactor() const { return m_zoomFactor; }

signals:
    // Emitted after the application font is re-applied for a zoom change (Ctrl +/-).
    // QSS-styled widgets that don't get automatic font propagation listen for this
    // to re-assert the zoom font explicitly (see DownloadsTab).
    void zoomChanged();

private:
    double m_zoomFactor = 1.0;
    void applyZoom();
    void saveZoom();
    void loadZoom();
};
