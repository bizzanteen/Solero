#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QString>

namespace solero {

// Makes NuGet restores work inside a Proton prefix.
//
// Under Proton .NET thinks it's on Windows and enforces repository-signature
// verification, which needs the VeriSign Universal Root CA (SHA1
// 3679CA35668772304D30A5FB873B0FA77BB70D54). Fedora, Debian and Arch have all
// dropped it, and Wine seeds the prefix store from the host bundle, so every
// restore fails with NU3028 + NU3037.
//
// Prefix-scoped: writes into the prefix's own HKLM store and touches nothing on
// the host. Those distros distrusted the root deliberately, so this is offered,
// never applied silently.
namespace PrefixCertFix {

// The VeriSign Universal Root CA in DER form (1213 bytes), bundled so the fix works
// with no network. Byte-identical to https://cacerts.digicert.com/universal-root.crt.pem.
QByteArray universalRootDer();

// SHA1 thumbprint as uppercase hex with no separators - the key name Windows and
// Wine give a certificate inside a system store.
QString thumbprint(const QByteArray& der);

// A certificate as Windows stores it: consecutive property records, each
// <u32 propId><u32 encoding><u32 cbData><data>, little-endian. We write property 3
// (the SHA1 hash) then property 32 (the DER), matching the entries Wine writes for
// the roots it imports from the host bundle.
QByteArray certBlob(const QByteArray& der);

// REGEDIT4 text that installs `der` into HKLM's Root store, ready for `reg.exe import`.
QString registryFileText(const QByteArray& der);

// True iff `winePrefix` (a compatdata dir, containing pfx/) already trusts the cert.
// A missing or unreadable prefix reads as not installed.
bool isInstalled(const QString& winePrefix, const QByteArray& der);

struct Result {
    bool ok = false;
    bool alreadyPresent = false;  // nothing was changed
    QString error;
};

// Imports the certificate into the prefix's Root store via Proton's bundled wine
// (`reg.exe import`, then `wineboot -s` to flush), and verifies it landed.
// Idempotent: returns {ok, alreadyPresent} without running anything when the prefix
// already trusts it. `log`, if given, collects the command output for diagnostics.
Result install(const QString& winePrefix, QString* log = nullptr);

// Does this captured tool output show the NuGet failure the missing root causes?
// Matches the two codes that appear together in that specific failure and nothing
// else, so an unrelated NuGet error doesn't get offered a certificate fix.
bool outputShowsNuGetTrustFailure(const QString& output);

// The same check against a tool's log files: Synthesis logs to
// <installDir>/logs/Current.log and writes nothing useful to stdout. Only logs
// modified at or after `since`, so a stale one can't re-trigger the offer.
bool toolLogShowsNuGetTrustFailure(const QString& installDir, const QDateTime& since);

} // namespace PrefixCertFix
} // namespace solero
