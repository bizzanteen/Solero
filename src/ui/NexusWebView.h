#pragma once
#include <QWidget>
#include <QUrl>

class QLineEdit;
class QToolButton;
class QTabWidget;
QT_BEGIN_NAMESPACE
class QWebEngineView;
class QWebEngineProfile;
class QWebChannel;
QT_END_NAMESPACE

namespace solero {

// Receives the credentials the sign-in page's JS hands back when the user submits
// the form. Registered on the web channel only while a sign-in page is open, so
// nexusmods.com's own scripts can't reach it from anywhere else.
class NexusLoginCapture : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    QString login, password;
public slots:
    void submitted(const QString& user, const QString& pass) { login = user; password = pass; }
};

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
    // A file the website downloaded itself (the plain "Download" button, not the
    // nxm one) has landed in the downloads folder. Carries whatever Nexus identity
    // could be recovered so the owner can write the sidecar.
    void fileDownloaded(const QString& path, const QString& modId,
                        const QString& fileId, const QString& version);
    void downloadProgress(const QString& fileName, qint64 received, qint64 total);

private:
    QWebEngineView* currentView() const;
    // Build a fully-wired tab (view + NxmPage + signal connections) and add it to
    // the tab bar, raising it only when foreground. Does not load a URL - used both
    // by addTab() and by NxmPage::createWindow() for ctrl/middle/_blank new tabs.
    QWebEngineView* createTabView(bool foreground);
    QWebEngineView* addTab(const QUrl& url);
    void loadAddress();
    void refreshNav();
    static QUrl homepageUrl();
    static QUrl signInUrl();
    bool looksLoggedIn() const;
    // Drop the saved login into the sign-in form, and arm the capture that offers
    // to save it when there is nothing stored yet.
    void setUpLoginAutofill(QWebEngineView* view);
    void setUpDownloadCapture();
    void offerToSaveLogin();

    QWebChannel*        m_channel = nullptr;
    NexusLoginCapture*  m_capture = nullptr;
    bool                m_savePromptOpen = false;

    QWebEngineProfile* m_profile = nullptr;
    QTabWidget*        m_tabs = nullptr;
    QLineEdit*         m_addr = nullptr;
    QToolButton*       m_back = nullptr;
    QToolButton*       m_fwd  = nullptr;
};

} // namespace solero
