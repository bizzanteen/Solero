#pragma once
#include <QWidget>
#include <QUrl>

class QLineEdit;
class QToolButton;
class QTabWidget;
QT_BEGIN_NAMESPACE
class QWebEngineView;
class QWebEngineProfile;
QT_END_NAMESPACE

namespace solero {

// An embedded Chromium browser for nexusmods.com with multiple tabs. All tabs
// share one persistent profile, so the Nexus login is shared across tabs and
// survives restarts. "Mod Manager Download" links (nxm://) are intercepted and
// routed out via nxmRequested() instead of navigated.
class NexusWebView : public QWidget {
    Q_OBJECT
public:
    explicit NexusWebView(QWidget* parent = nullptr);

signals:
    void nxmRequested(const QString& url);

private:
    QWebEngineView* currentView() const;
    QWebEngineView* addTab(const QUrl& url);
    void loadAddress();
    void refreshNav();
    static QUrl homepageUrl();
    static QUrl signInUrl();
    bool looksLoggedIn() const;

    QWebEngineProfile* m_profile = nullptr;
    QTabWidget*        m_tabs = nullptr;
    QLineEdit*         m_addr = nullptr;
    QToolButton*       m_back = nullptr;
    QToolButton*       m_fwd  = nullptr;
};

} // namespace solero
