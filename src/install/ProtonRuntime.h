#pragma once
#include <QString>
#include <QLatin1String>

namespace solero {

// An off-site mod requirement that is actually a Windows runtime component
// (VC++ redistributable, .NET, XNA, DirectX). Proton ships these in the game's
// prefix, so they almost never need separate installation on Linux - and they
// can never be installed as mod files (they live in the Wine prefix, not Data).
//
// If `url`/`name`/`notes` describe such a runtime, return a friendly display
// name; otherwise return an empty string (a genuine off-Nexus mod/tool).
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
