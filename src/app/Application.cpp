#include "Application.h"
#include <QSettings>
#include <QFont>

Application::Application(int& argc, char** argv) : QApplication(argc, argv) {
    setApplicationName("Solero");
    setOrganizationName("Solero");
    loadZoom();
}

void Application::setZoomFactor(double factor) {
    m_zoomFactor = qBound(0.5, factor, 3.0);
    applyZoom();
    saveZoom();
}

void Application::applyZoom() {
    QFont f = font();
    f.setPointSizeF(10.0 * m_zoomFactor);
    setFont(f);
}

void Application::saveZoom() {
    QSettings s;
    s.setValue("ui/zoomFactor", m_zoomFactor);
}

void Application::loadZoom() {
    QSettings s;
    m_zoomFactor = s.value("ui/zoomFactor", 1.0).toDouble();
    applyZoom();
}
