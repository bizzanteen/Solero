#include "Linker.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <filesystem>

namespace solero {

Linker::Linker(DeployMode mode) : m_mode(mode) {}

bool Linker::deploy(const QString& src, const QString& dst) const {
    QDir().mkpath(QFileInfo(dst).path());

    // Remove existing target first (hardlink/copy can't overwrite)
    if (QFile::exists(dst) || QFileInfo(dst).isSymLink())
        QFile::remove(dst);

    switch (m_mode) {
        case DeployMode::HardLink:
            {
                std::filesystem::path s(src.toStdString());
                std::filesystem::path d(dst.toStdString());
                std::error_code ec;
                std::filesystem::create_hard_link(s, d, ec);
                if (ec) {
                    // Fallback to copy if hardlink fails (cross-device)
                    return QFile::copy(src, dst);
                }
                return true;
            }
        case DeployMode::SymLink:
            return QFile::link(src, dst);
        case DeployMode::Copy:
            return QFile::copy(src, dst);
    }
    return false;
}

bool Linker::remove(const QString& dst) const {
    if (!QFile::exists(dst) && !QFileInfo(dst).isSymLink())
        return true;
    return QFile::remove(dst);
}

} // namespace solero
