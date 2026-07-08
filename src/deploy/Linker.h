#pragma once
#include "DeployMode.h"
#include <QString>

namespace solero {

class Linker {
public:
    explicit Linker(DeployMode mode = DeployMode::HardLink);

    // Place src at dst, creating parent directories. Overwrites if dst exists.
    bool deploy(const QString& src, const QString& dst) const;

    // True when dst is ALREADY the correct deployment of src, so no relink is needed.
    // HardLink mode: dst and src share a device+inode (identical bytes, same mtime).
    // Copy/SymLink: no cheap identity check, so always false (caller relinks). This is
    // a ground-truth guard that skips redundant unlink+relink work on a no-op deploy.
    bool alreadyDeployed(const QString& src, const QString& dst) const;

    // Remove dst if it exists. Returns true if removed or already absent.
    bool remove(const QString& dst) const;

    DeployMode mode() const { return m_mode; }

private:
    DeployMode m_mode;
};

} // namespace solero
