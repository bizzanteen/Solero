#include "NexusWebView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLineEdit>
#include <QUrl>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineHistory>
#include <functional>

namespace solero {

// A page that intercepts nxm:// navigations (the "Mod Manager Download" buttons)
// and forwards them via a callback instead of navigating to an unknown scheme.
// No Q_OBJECT here (avoids a per-.cpp .moc) - the callback emits the view's signal.
class NxmPage : public QWebEnginePage {
public:
    NxmPage(QWebEngineProfile* profile, QObject* parent,
            std::function<void(const QString&)> onNxm)
        : QWebEnginePage(profile, parent), m_onNxm(std::move(onNxm)) {}
protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override {
        if (url.scheme().compare("nxm", Qt::CaseInsensitive) == 0) {
            if (m_onNxm) m_onNxm(url.toString());
            return false;
        }
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }
private:
    std::function<void(const QString&)> m_onNxm;
};

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

    // Web view (persistent profile so the Nexus login survives)
    auto* profile = new QWebEngineProfile(QStringLiteral("solero-nexus"), this);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);

    m_view = new QWebEngineView(this);
    auto* page = new NxmPage(profile, m_view,
                             [this](const QString& url){ emit nxmRequested(url); });
    m_view->setPage(page);
    v->addWidget(m_view, 1);

    connect(m_back, &QToolButton::clicked, m_view, &QWebEngineView::back);
    connect(m_fwd,  &QToolButton::clicked, m_view, &QWebEngineView::forward);
    connect(reload, &QToolButton::clicked, m_view, &QWebEngineView::reload);
    connect(home,   &QToolButton::clicked, this, [this]{
        m_view->load(QUrl(QStringLiteral("https://www.nexusmods.com/skyrimspecialedition")));
    });
    connect(m_addr, &QLineEdit::returnPressed, this, &NexusWebView::loadAddress);
    connect(m_view, &QWebEngineView::urlChanged, this, [this](const QUrl& u){
        if (!m_addr->hasFocus()) m_addr->setText(u.toString());
        m_back->setEnabled(m_view->history()->canGoBack());
        m_fwd->setEnabled(m_view->history()->canGoForward());
    });

    m_back->setEnabled(false);
    m_fwd->setEnabled(false);
    m_view->load(QUrl(QStringLiteral("https://www.nexusmods.com/skyrimspecialedition")));
}

void NexusWebView::loadAddress() {
    QString t = m_addr->text().trimmed();
    if (t.isEmpty()) return;
    if (!t.contains("://")) t = "https://" + t;
    m_view->load(QUrl::fromUserInput(t));
}

} // namespace solero
