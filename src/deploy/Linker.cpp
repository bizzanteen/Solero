#include "Linker.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <filesystem>
#include <sys/stat.h>

namespace solero {

Linker::Linker(DeployMode mode) : m_mode(mode) {}

bool Linker::alreadyDeployed(const QString& src, const QString& dst) const {
    if (m_mode != DeployMode::HardLink) return false; // only hardlinks share an inode
    struct stat ss, ds;
    if (::stat(src.toLocal8Bit().constData(), &ss) != 0) return false;   // src: real file
    if (::lstat(dst.toLocal8Bit().constData(), &ds) != 0) return false;  // dst: no symlink follow
    // Same device + inode => dst is already a hardlink to src, so its bytes and mtime
    // match by definition and there is nothing to do.
    return ss.st_dev == ds.st_dev && ss.st_ino == ds.st_ino;
}

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
