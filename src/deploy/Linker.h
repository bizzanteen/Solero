#pragma once
#include "DeployMode.h"
#include <QString>

namespace solero {

class Linker {
public:
    explicit Linker(DeployMode mode = DeployMode::HardLink);

    // Place src at dst, creating parent directories. Overwrites if dst exists.
    bool deploy(const QString& src, const QString& dst) const;

    // Remove dst if it exists. Returns true if removed or already absent.
    bool remove(const QString& dst) const;

    DeployMode mode() const { return m_mode; }

private:
    DeployMode m_mode;
};

} // namespace solero
