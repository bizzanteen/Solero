#pragma once
#include <QDialog>
#include <QString>

class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;

namespace solero {

// Identify a mod on Nexus by name when the MD5 route isn't available - no source
// archive on disk, or the hash matched nothing. Searches by name, then asks which
// file, because a mod page holds many files at their own versions and guessing
// would re-download something other than what is installed.
class NexusIdentifyDialog : public QDialog {
    Q_OBJECT
public:
    // knownModId short-circuits the search: ensureNexusIds also fires when only
    // the FILE id is missing, and making the user hunt for a mod Solero has
    // already identified would be busywork.
    NexusIdentifyDialog(const QString& modName, const QString& installedVersion,
                        const QString& knownModId = {}, QWidget* parent = nullptr);

    QString chosenModId() const   { return m_modId; }
    QString chosenFileId() const  { return m_fileId; }
    QString chosenVersion() const { return m_version; }

private:
    void runSearch();
    void loadFiles();
    void updateOkState();

    QString m_installedVersion;
    QString m_modId, m_fileId, m_version;

    QLineEdit*   m_query = nullptr;
    QListWidget* m_mods = nullptr;
    QListWidget* m_files = nullptr;
    QLabel*      m_status = nullptr;
    QPushButton* m_ok = nullptr;
};

} // namespace solero
