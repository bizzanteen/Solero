#pragma once
#include <QString>
#include <QLatin1String>

namespace solero {

// Off-site requirements that are really Windows runtimes (VC++, .NET, XNA,
// DirectX). Proton ships these in the prefix, and they could never install as mod
// files anyway. Returns a display name, or empty for a genuine off-Nexus mod.
inline QString protonProvidedRuntime(const QString& url, const QString& name,
                                     const QString& notes) {
    const QString h = (url + QLatin1Char(' ') + name + QLatin1Char(' ') + notes).toLower();
    auto has = [&](const char* s) { return h.contains(QLatin1String(s)); };

    if (has("vc-redist") || has("vc_redist") || has("vcredist")
        || has("visual c++") || has("vcrun"))
        return QStringLiteral("Microsoft Visual C++ Redistributable");
    if (has("dotnet") || has(".net framework") || has(".net runtime")
        || has(".net desktop") || has(".net core"))
        return QStringLiteral("Microsoft .NET Runtime");
    if (has("xna"))
        return QStringLiteral("Microsoft XNA Framework");
    if (has("directx") || has("dxsetup") || has("dxwebsetup") || has("d3dx"))
        return QStringLiteral("DirectX Runtime");
    return {};
}

// Canonical protontricks docs (the user may want to confirm the runtime is
// configured in the prefix if a mod that depends on it misbehaves).
inline QString protontricksDocsUrl() {
    return QStringLiteral("https://github.com/Matoking/protontricks#readme");
}

} // namespace solero
