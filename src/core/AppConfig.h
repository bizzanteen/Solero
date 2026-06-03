#pragma once
#include <QString>

namespace solero {

class AppConfig {
public:
    static AppConfig& instance();

    bool isConfigured() const;
    bool load();
    bool save() const;

    const QString& gameDir() const     { return m_gameDir; }
    const QString& stagingDir() const  { return m_stagingDir; }
    const QString& dataDir() const     { return m_dataDir; }

    void setGameDir(const QString& p);
    void setStagingDir(const QString& p);

    static QString configPath();
    static QStringList detectSkyrimPaths();

private:
    AppConfig() = default;
    QString m_gameDir;
    QString m_stagingDir;
    QString m_dataDir; // derived: gameDir/Data
};

} // namespace solero
