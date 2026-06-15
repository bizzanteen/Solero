#include "NexusWebView.h"
#include "nexus/NexusApi.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QToolButton>
#include <QLineEdit>
#include <QUrl>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineHistory>
#include <QFileInfo>
#include <QDir>
#include <QIcon>
#include <QRegularExpression>
#include <functional>

namespace solero {

// A page that intercepts nxm:// navigations (the "Mod Manager Download" buttons)
// and forwards them via a callback instead of navigating to an unknown scheme.
// No Q_OBJECT here (avoids a per-.cpp .moc) - the callback emits the view's signal.
class NxmPage : public QWebEnginePage {
public:
    NxmPage(QWebEngineProfile* profile, QObject* parent,
            std::function<void(const QString&)> onNxm,
            std::function<QWebEnginePage*(bool background)> onNewTab)
        : QWebEnginePage(profile, parent),
          m_onNxm(std::move(onNxm)), m_onNewTab(std::move(onNewTab)) {}
protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override {
        if (url.scheme().compare("nxm", Qt::CaseInsensitive) == 0) {
            if (m_onNxm) m_onNxm(url.toString());
            return false;
        }
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }
    // createWindow is the hook QtWebEngine calls for links that want a new view:
    // middle-click, Ctrl-click (both WebBrowserBackgroundTab) and target="_blank"
    // (WebBrowserTab/WebDialog). We hand back a real new-tab page so the engine
    // loads the target into it; returning nullptr is what made those clicks no-op.
    QWebEnginePage* createWindow(WebWindowType type) override {
        if (!m_onNewTab) return nullptr;
        const bool background = (type == QWebEnginePage::WebBrowserBackgroundTab);
        return m_onNewTab(background);
    }
private:
    std::function<void(const QString&)> m_onNxm;
    std::function<QWebEnginePage*(bool background)> m_onNewTab;
};

QUrl NexusWebView::homepageUrl() {
    return QUrl(QStringLiteral("https://www.nexusmods.com/skyrimspecialedition"));
}

QUrl NexusWebView::signInUrl() {
    return QUrl(QStringLiteral(
        "https://users.nexusmods.com/auth/sign_in?redirect_url=https://www.nexusmods.com/skyrimspecialedition"));
}

QUrl NexusWebView::apiKeyUrl() {
    return QUrl(QStringLiteral("https://www.nexusmods.com/users/myaccount?tab=api"));
}

bool NexusWebView::looksLoggedIn() const {
    if (!m_profile) return false;
    const QString base = m_profile->persistentStoragePath();
    if (base.isEmpty()) return false;

    // The Cookies file may live directly in the storage path…
    QFileInfo direct(QDir(base).filePath(QStringLiteral("Cookies")));
    if (direct.exists() && direct.size() > 100) return true;

    // …or one level down in a subdirectory.
    QDir d(base);
    const auto subs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& sub : subs) {
        QFileInfo fi(d.filePath(sub + QStringLiteral("/Cookies")));
        if (fi.exists() && fi.size() > 100) return true;
    }
    return false;
}

NexusWebView::NexusWebView(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // Nav bar
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(4, 4, 4, 4);
    m_back = new QToolButton(this); m_back->setText("\xe2\x97\x80"); m_back->setToolTip("Back");
    m_fwd  = new QToolButton(this); m_fwd->setText("\xe2\x96\xb6");  m_fwd->setToolTip("Forward");
    auto* reload = new QToolButton(this); reload->setText("\xe2\x9f\xb3"); reload->setToolTip("Reload");
    auto* home   = new QToolButton(this); home->setText("\xe2\x8c\x82"); home->setToolTip("Nexus home");
    m_addr = new QLineEdit(this);
    m_addr->setClearButtonEnabled(true);
    m_addr->setPlaceholderText("nexusmods.com\xe2\x80\xa6");
    bar->addWidget(m_back);
    bar->addWidget(m_fwd);
    bar->addWidget(reload);
    bar->addWidget(home);
    bar->addWidget(m_addr, 1);
    v->addLayout(bar);

    // Shared persistent profile (Nexus login survives across tabs/restarts)
    m_profile = new QWebEngineProfile(QStringLiteral("solero-nexus"), this);
    m_profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    // Nexus's CDN serves anti-bot challenge pages (broken JS chunks, blocked GraphQL
    // search) to the default QtWebEngine UA because of its "QtWebEngine/x.y.z" token.
    // Drop that token so we present as the plain Chrome the engine is built on.
    QString ua = m_profile->httpUserAgent();
    ua.remove(QRegularExpression(QStringLiteral("\\s*QtWebEngine/\\S+")));
    m_profile->setHttpUserAgent(ua);

    // Tabs
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    v->addWidget(m_tabs, 1);

    auto* newTabBtn = new QToolButton(this);
    newTabBtn->setText(QStringLiteral("+"));
    newTabBtn->setToolTip(QStringLiteral("New tab"));
    m_tabs->setCornerWidget(newTabBtn);
    connect(newTabBtn, &QToolButton::clicked, this, [this]{ addTab(homepageUrl()); });

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int index){
        QWidget* w = m_tabs->widget(index);
        if (m_tabs->count() <= 1) {
            // Don't leave zero tabs - open a fresh homepage tab, then close this one.
            addTab(homepageUrl());
        }
        m_tabs->removeTab(m_tabs->indexOf(w));
        delete w;
    });

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int){ refreshNav(); });

    // Nav bar drives the current view
    connect(m_back, &QToolButton::clicked, this, [this]{ if (auto* vw = currentView()) vw->back(); });
    connect(m_fwd,  &QToolButton::clicked, this, [this]{ if (auto* vw = currentView()) vw->forward(); });
    connect(reload, &QToolButton::clicked, this, [this]{ if (auto* vw = currentView()) vw->reload(); });
    connect(home,   &QToolButton::clicked, this, [this]{ if (auto* vw = currentView()) vw->load(homepageUrl()); });
    connect(m_addr, &QLineEdit::returnPressed, this, &NexusWebView::loadAddress);

    m_back->setEnabled(false);
    m_fwd->setEnabled(false);

    // First tab: sign-in if we don't look logged in, else the homepage.
    addTab(looksLoggedIn() ? homepageUrl() : signInUrl());
}

QWebEngineView* NexusWebView::currentView() const {
    return qobject_cast<QWebEngineView*>(m_tabs->currentWidget());
}

QWebEngineView* NexusWebView::createTabView(bool foreground) {
    auto* view = new QWebEngineView(m_tabs);
    // The onNewTab callback captures `this` (not `view`): when a link inside this
    // page opens a new tab it asks the NexusWebView to build another fully-wired
    // tab and hands its page back to the engine. No infinite recursion - the
    // callback only fires on a real user click, never during construction here.
    auto* page = new NxmPage(m_profile, view,
                             [this](const QString& u){ emit nxmRequested(u); },
                             [this](bool background) -> QWebEnginePage* {
                                 auto* v = createTabView(!background);
                                 return v->page();
                             });
    view->setPage(page);

    connect(view, &QWebEngineView::urlChanged, this, [this, view](const QUrl& u){
        if (view == currentView()) {
            if (!m_addr->hasFocus()) m_addr->setText(u.toString());
            m_back->setEnabled(view->history()->canGoBack());
            m_fwd->setEnabled(view->history()->canGoForward());
        }
    });

    connect(view, &QWebEngineView::titleChanged, this, [this, view](const QString& title){
        int idx = m_tabs->indexOf(view);
        if (idx < 0) return;
        QString t = title.trimmed();
        if (t.isEmpty()) t = QStringLiteral("Nexus");
        if (t.size() > 20) t = t.left(19) + QChar(0x2026); // U+2026 HORIZONTAL ELLIPSIS
        m_tabs->setTabText(idx, t);
    });

    connect(view->page(), &QWebEnginePage::iconChanged, this, [this, view](const QIcon& icon){
        int idx = m_tabs->indexOf(view);
        if (idx >= 0) m_tabs->setTabIcon(idx, icon);
    });

    int idx = m_tabs->addTab(view, QStringLiteral("Nexus"));
    if (foreground) m_tabs->setCurrentIndex(idx);
    return view;
}

QWebEngineView* NexusWebView::addTab(const QUrl& url) {
    auto* view = createTabView(true);
    view->load(url);
    return view;
}

void NexusWebView::refreshNav() {
    auto* vw = currentView();
    if (!vw) {
        m_back->setEnabled(false);
        m_fwd->setEnabled(false);
        return;
    }
    if (!m_addr->hasFocus()) m_addr->setText(vw->url().toString());
    m_back->setEnabled(vw->history()->canGoBack());
    m_fwd->setEnabled(vw->history()->canGoForward());
}

void NexusWebView::loadAddress() {
    auto* vw = currentView();
    if (!vw) return;
    QString t = m_addr->text().trimmed();
    if (t.isEmpty()) return;
    if (!t.contains("://")) t = "https://" + t;
    vw->load(QUrl::fromUserInput(t));
}

void NexusWebView::openUrl(const QUrl& url) {
    addTab(url);
}


} // namespace solero
