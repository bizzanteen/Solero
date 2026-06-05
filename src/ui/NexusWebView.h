#pragma once
#include <QWidget>

class QLineEdit;
class QToolButton;
QT_BEGIN_NAMESPACE
class QWebEngineView;
QT_END_NAMESPACE

namespace solero {

// An embedded Chromium browser for nexusmods.com. Logins persist (a named
// persistent profile keeps cookies), and "Mod Manager Download" links (nxm://)
// are intercepted and routed out via nxmRequested() instead of navigated.
class NexusWebView : public QWidget {
    Q_OBJECT
public:
    explicit NexusWebView(QWidget* parent = nullptr);

signals:
    void nxmRequested(const QString& url);

private:
    void loadAddress();
    QWebEngineView* m_view = nullptr;
    QLineEdit*      m_addr = nullptr;
    QToolButton*    m_back = nullptr;
    QToolButton*    m_fwd  = nullptr;
};

} // namespace solero
