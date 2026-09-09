#pragma once
#include <QString>
#include <QUrl>

namespace solero {

// Nexus sign-in page helpers. Pure, so the URL matching and the JS quoting are
// testable without a browser.

// The sign-in form lives at users.nexusmods.com/auth/sign_in, usually with a
// redirect_url query. Matches the host too - a mod page that merely mentions the
// path must not trigger a fill.
inline bool isNexusSignInUrl(const QUrl& url) {
    if (url.host().compare(QLatin1String("users.nexusmods.com"), Qt::CaseInsensitive) != 0)
        return false;
    const QString path = url.path();
    return path.compare(QLatin1String("/auth/sign_in"), Qt::CaseInsensitive) == 0
        || path.compare(QLatin1String("/auth/sign_in/"), Qt::CaseInsensitive) == 0;
}

// Quote a value for embedding in a JS single-quoted string. Passwords routinely
// contain quotes and backslashes, and a stray one would break the script (or worse,
// end the literal early), so escape rather than trust the input.
inline QString jsQuote(const QString& raw) {
    QString out;
    out.reserve(raw.size() + 8);
    out += QLatin1Char('\'');
    for (const QChar c : raw) {
        switch (c.unicode()) {
        case '\\': out += QLatin1String("\\\\"); break;
        case '\'': out += QLatin1String("\\'");  break;
        case '\n': out += QLatin1String("\\n");  break;
        case '\r': out += QLatin1String("\\r");  break;
        case 0x2028: out += QLatin1String("\\u2028"); break;  // JS line separators
        case 0x2029: out += QLatin1String("\\u2029"); break;
        default:   out += c;                     break;
        }
    }
    out += QLatin1Char('\'');
    return out;
}

// Script that drops the saved login into the two fields. Fires the events React
// listens for, otherwise the framework keeps its own empty state and submits blank.
// Never touches the submit button - Turnstile and any 2FA step are the user's.
inline QString buildFillScript(const QString& login, const QString& password) {
    return QStringLiteral(R"JS((function(){
  var u = document.getElementById('user_login');
  var p = document.getElementById('password');
  if (!u || !p) return false;
  function set(el, val) {
    var d = Object.getOwnPropertyDescriptor(el.__proto__, 'value');
    if (d && d.set) d.set.call(el, val); else el.value = val;
    el.dispatchEvent(new Event('input',  { bubbles: true }));
    el.dispatchEvent(new Event('change', { bubbles: true }));
  }
  set(u, %1);
  set(p, %2);
  return true;
})())JS").arg(jsQuote(login), jsQuote(password));
}

} // namespace solero
