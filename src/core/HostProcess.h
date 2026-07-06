#pragma once
#include <QString>
#include <QStringList>
#include <QMap>

// Flatpak sandbox awareness. Solero's whole job is launching HOST processes - Steam,
// umu-run/Proton, the game exe, external tools, gsettings, curl - none of which exist
// inside a Flatpak sandbox. Inside Flatpak we run them on the host via
// `flatpak-spawn --host` (which needs `--talk-name=org.freedesktop.Flatpak` in the
// manifest). Outside Flatpak everything is unchanged.

namespace solero {

// True when running inside a Flatpak sandbox (the runtime drops /.flatpak-info in).
bool runningInFlatpak();

struct HostCommand {
    QString program;
    QStringList args;
    // Native: the caller should apply `envOverrides` onto the QProcess environment.
    // Flatpak: the overrides were folded into the argv as `--env=K=V` and the host
    // process inherits the host environment, so the caller must not set a sandbox env.
    bool applyEnvToProcess = true;
};

// Rewrite (program, args, envOverrides) so the command runs on the HOST when
// `inFlatpak`. `inFlatpak` is a parameter (not read internally) so the mapping is
// pure and unit-testable; pass runningInFlatpak() at the call site.
HostCommand hostCommand(const QString& program, const QStringList& args,
                        const QMap<QString, QString>& envOverrides,
                        bool inFlatpak);

} // namespace solero
