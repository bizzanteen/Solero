#pragma once
#include "wabbajack/WabbajackModlist.h"
#include <QDialog>
#include <QList>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QPixmap>

class QStackedWidget;
class QLineEdit;
class QComboBox;
class QMenu;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QTextBrowser;
class QProgressBar;
class QPlainTextEdit;
class QNetworkAccessManager;
class QNetworkReply;

namespace solero {

class ProfileManager;
class WabbajackEngine;

// Wabbajack modlist gallery + install UI. Drives the WabbajackEngine (which
// runs jackify-engine) to list the gallery and install a modlist, then imports
// the produced MO2-portable instance as a Solero profile via Mo2Importer.
class WabbajackDialog : public QDialog {
    Q_OBJECT
public:
    explicit WabbajackDialog(ProfileManager* profiles, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;
    void reject() override;

signals:
    void profileImported(const QString& profileName);

private slots:
    void onModlistsReady(const QList<WabbajackModlist>& modlists);
    void onFailed(const QString& error);
    void onProgress(const QString& op, const QString& file, double pct);
    void onLogLine(const QString& line);
    void onInstallFinished(bool ok, int exitCode);

private:
    void buildGalleryPage();
    void buildProgressPage();
    void startFetch();
    void showEngineMissing();
    void applyFilter();
    void onSelectionChanged();
    void loadThumb(QListWidgetItem* item, const QString& url);
    static QString thumbCachePath(const QString& url); // dataRoot()/wabbajack-thumbs/<md5>.png
    void triggerInstall(const QString& target, bool isLocalFile, const QString& displayName);
    // Kick off (or resume) the install with the remembered m_install* parameters.
    void startInstallRun();
    void doImport();
    // Returns true if it's OK to close/reject (not installing, or user confirmed
    // cancelling the in-progress install - in which case the engine is cancelled).
    bool confirmCloseWhileInstalling();

    static QString sanitize(const QString& s);

public:
    // Official/Unofficial filter selector.
    enum class OfficialFilter { All, Official, Unofficial };

    // Pure, headless-testable filter predicate: returns true if `ml` should be
    // shown given the active search query, official filter, and the set of
    // selected tags (empty = "any tag"). Composes search + official + tags; the
    // caller still applies the game filter separately.
    static bool passesFilters(const WabbajackModlist& ml, const QString& searchQ,
                              OfficialFilter official, const QSet<QString>& selectedTags);

    // Union of all tags across `lists`, sorted, de-duplicated (case-sensitive keys).
    static QStringList collectTags(const QList<WabbajackModlist>& lists);

private:
    void rebuildTagMenu();
    void updateTagButtonLabel();

    ProfileManager* m_profiles = nullptr;
    WabbajackEngine* m_engine = nullptr;
    QNetworkAccessManager* m_net = nullptr;

    QStackedWidget* m_stack = nullptr;

    // Page 0 - gallery
    QWidget* m_galleryPage = nullptr;
    QLineEdit* m_search = nullptr;
    QComboBox* m_officialCombo = nullptr;  // All / Official / Unofficial
    QPushButton* m_tagBtn = nullptr;       // tag filter dropdown
    QMenu* m_tagMenu = nullptr;
    QSet<QString> m_selectedTags;          // empty = any tag
    QPushButton* m_refreshBtn = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_statusLabel = nullptr;      // "Loading…" / error placeholder
    QPushButton* m_retryBtn = nullptr;
    // details pane
    QLabel* m_detailImage = nullptr;
    QLabel* m_detailTitle = nullptr;
    QLabel* m_detailMeta = nullptr;
    QTextBrowser* m_detailDesc = nullptr;
    QPushButton* m_readmeBtn = nullptr;
    QPushButton* m_installBtn = nullptr;

    QList<WabbajackModlist> m_all;        // full fetched gallery
    QList<WabbajackModlist> m_filtered;   // current view (index-aligned to m_list rows)
    int m_thumbGen = 0;                   // guards async thumbnail replies
    QHash<QString, QPixmap> m_thumbMem;   // in-session thumbnail cache, keyed by url

    // Page 1 - install progress
    QWidget* m_progressPage = nullptr;
    QLabel* m_progTitle = nullptr;
    QProgressBar* m_progBar = nullptr;
    QLabel* m_progOp = nullptr;
    QPlainTextEdit* m_progLog = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_backBtn = nullptr;

    // remembered for the import step after a successful install
    QString m_installDir;
    QString m_installTitle;
    bool m_installing = false;
    // Remembered install args so a failed install can be retried/resumed (the
    // engine resumes from the downloads dir) with the exact same parameters.
    QString m_installTarget;
    bool m_installIsLocalFile = false;
    QString m_installDownloadsDir;
    QPushButton* m_retryInstallBtn = nullptr;
};

} // namespace solero
