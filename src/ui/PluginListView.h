#pragma once
#include <QTableView>
#include <QItemSelection>
#include "core/Profile.h"
class QSortFilterProxyModel;
class QTimer;
namespace solero {
class PluginListModel;
class PluginListView : public QTableView {
    Q_OBJECT
public:
    explicit PluginListView(QWidget* parent = nullptr);
    void setProfile(Profile* profile);
    void reconcileWith(Profile* profile, const QString& stagingRoot);
    void highlightPlugins(const QStringList& filenames);
    // Select + scroll to the plugin row matching `filename` (case-insensitive),
    // mapping through the sort proxy when active. No-op if not found.
    void selectPlugin(const QString& filename);
    // Filter the visible plugins by name (case-insensitive). Non-empty routes the
    // view through the proxy and suspends drag-reorder; empty restores both.
    void setFilter(const QString& text);
signals:
    // Forwarded from the model: the user manually reordered the load order.
    void loadOrderChanged();
    // Forwarded from the model: a plugin's enabled state was toggled.
    void pluginEnabledChanged();
    // A plugin row became the current selection (single click / arrow keys):
    // carries the plugin filename so the mod pane can highlight its providers.
    void pluginClicked(const QString& filename);
    // A plugin row was double-clicked or "Go to origin mod" was chosen: navigate
    // the mod pane to the winning origin mod.
    void pluginActivated(const QString& filename);
private slots:
    void onSortChanged(int col, Qt::SortOrder order);
protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void selectionChanged(const QItemSelection& selected,
                          const QItemSelection& deselected) override;
private:
    void applyHeaderLayout();
    // Persist the current header layout (column widths) to AppConfig; debounced via
    // m_headerSaveTimer so a resize drag writes config once, not per pixel.
    void saveHeaderState();
    QTimer* m_headerSaveTimer = nullptr;
    void autoSizeColumns();
    void setAllEnabled(bool enabled);
    // Filename of the row at the given view index (proxy-aware, pin-glyph-free),
    // or the current row when `idx` is invalid. Empty if not a real plugin row.
    QString pluginFilenameAt(const QModelIndex& idx) const;
    PluginListModel* m_model;
    QSortFilterProxyModel* m_proxy;
    bool m_didAutoSize = false;
    bool m_filterActive = false;
};
}
