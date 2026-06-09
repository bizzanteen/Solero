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

    // Open a URL in a fresh tab and raise it. Used to jump straight to the
    // personal API-key page from the "Connect to Nexus" Settings button.
    void openUrl(const QUrl& url);

    // The personal API-key page (login cookie persists in this profile).
    static QUrl apiKeyUrl();

signals:
    void nxmRequested(const QString& url);

private:
    QWebEngineView* currentView() const;
    // Build a fully-wired tab (view + NxmPage + signal connections) and add it to
    // the tab bar, raising it only when foreground. Does not load a URL - used both
    // by addTab() and by NxmPage::createWindow() for ctrl/middle/_blank new tabs.
    QWebEngineView* createTabView(bool foreground);
    QWebEngineView* addTab(const QUrl& url);
    void loadAddress();
    // Read the clipboard, validate it as a Nexus API key, store it, and report
    // the result inline. Driven by the "Paste key & connect" toolbar button.
    void pasteKeyAndConnect();
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
