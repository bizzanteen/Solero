#include "ModListView.h"
#include "ModListModel.h"
#include "SeparatorDialog.h"
#include "install/DependencyChecker.h"
#include "core/AppConfig.h"
#include "core/Profile.h"
#include "ui/IconUtil.h"
#include <QMenu>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QInputDialog>
#include <QUuid>
#include <QItemSelectionModel>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFrame>
#include <QListWidget>
#include <QWidgetAction>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QPainter>
#include <QApplication>
#include <QIcon>
#include <QLineEdit>
#include <QShowEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QTimer>
#include <algorithm>

namespace solero {

namespace {
class RenameDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setEditorData(QWidget* editor, const QModelIndex& idx) const override {
        if (auto* le = qobject_cast<QLineEdit*>(editor)) {
            le->setText(idx.data(Qt::EditRole).toString());
            le->end(false); // cursor at end, nothing selected (instead of select-all)
        } else { QStyledItemDelegate::setEditorData(editor, idx); }
    }

    // The synthetic [Overwrite] row renders as "[Overwrite] - <ProfileName>": the
    // bracket keeps the model's colour (red when it has files, dark-yellow when
    // empty) and bold, while the profile name is appended in grey italics. A single
    // DisplayRole string can't carry two styles, so paint it ourselves.
    void paint(QPainter* p, const QStyleOptionViewItem& option,
               const QModelIndex& idx) const override {
        const QString prof = idx.data(ModListModel::OverwriteProfileRole).toString();
        if (prof.isEmpty()) { QStyledItemDelegate::paint(p, option, idx); return; }

        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, idx);
        opt.text.clear(); // draw background/selection only; we render the text below
        QStyle* style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, opt.widget);

        const QString bracket = idx.data(Qt::DisplayRole).toString(); // "[Overwrite]"
        QColor fg = qvariant_cast<QColor>(idx.data(Qt::ForegroundRole));
        if (!fg.isValid()) fg = opt.palette.color(QPalette::Text);
        QFont bf = qvariant_cast<QFont>(idx.data(Qt::FontRole));

        p->save();
        const QRect r = opt.rect.adjusted(4, 0, -4, 0);
        int x = r.left();

        QFontMetrics bfm(bf);
        p->setFont(bf);
        p->setPen(fg);
        p->drawText(QRect(x, r.top(), bfm.horizontalAdvance(bracket), r.height()),
                    Qt::AlignVCenter | Qt::AlignLeft, bracket);
        x += bfm.horizontalAdvance(bracket);

        const QString suffix = QStringLiteral("  - ") + prof; // "  - Name"
        QFont sf = bf; sf.setBold(false); sf.setItalic(true);
        QFontMetrics sfm(sf);
        p->setFont(sf);
        p->setPen(QColor(0x88, 0x88, 0x88)); // grey, regardless of the bracket colour
        p->drawText(QRect(x, r.top(), sfm.horizontalAdvance(suffix), r.height()),
                    Qt::AlignVCenter | Qt::AlignLeft, suffix);
        p->restore();
    }
};

// The Flags column carries a horizontal strip of status icons composed into one
// QIcon. The default delegate squeezes that whole strip into the square
// iconSize(), shrinking each icon when a row has 2-3 flags. Render the composed
// icon at its natural size instead so every flag stays at the canonical pixel
// size regardless of how many a row has.
class FlagsDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
protected:
    void initStyleOption(QStyleOptionViewItem* opt, const QModelIndex& idx) const override {
        QStyledItemDelegate::initStyleOption(opt, idx);
        if (!opt->icon.isNull()) {
            // actualSize keeps the strip at its drawn height (kFlagIconPx) and
            // returns its true width, so the base delegate draws it un-squished.
            const QSize nat = opt->icon.actualSize(QSize(1 << 16, opt->decorationSize.height()));
            if (!nat.isEmpty()) opt->decorationSize = nat;
        }
    }
};
} // namespace

ModListView::ModListView(QWidget* parent) : QTreeView(parent) {
    m_model = new ModListModel(this);
    setModel(m_model);
    connect(m_model, &ModListModel::modsChanged, this, &ModListView::modsChanged);
    // Model resets (rebuild()/setProfile()) clear first-column spans, so re-apply
    // them whenever the model is reset to keep separators full-width.
    connect(m_model, &QAbstractItemModel::modelReset, this, &ModListView::applyRowSpans);
    setRootIsDecorated(false);
    setIndentation(0); // remove the empty tree-indent column before the checkbox
    setDragDropMode(QAbstractItemView::InternalMove);
    // InternalMove normally implies these, but set them explicitly so the reorder
    // drag reliably starts and accepts drops.
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection); // Ctrl+click multi-select
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setAlternatingRowColors(true);
    setIconSize(QSize(20, 20));
    setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed);
    setItemDelegateForColumn(ModListModel::ColName, new RenameDelegate(this));
    setItemDelegateForColumn(ModListModel::ColFlags, new FlagsDelegate(this));
    // Every column is user-resizable (Interactive). A middle Stretch section makes
    // manual resizes feel inverted, so we instead auto-fit on first show.
    auto* hdr = header();
    hdr->setSectionResizeMode(ModListModel::ColEnabled,  QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColPriority, QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColName,     QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColVersion,  QHeaderView::Interactive);
    hdr->setSectionResizeMode(ModListModel::ColFlags,    QHeaderView::Interactive);
    hdr->setStretchLastSection(false);
    hdr->resizeSection(ModListModel::ColEnabled,  28);
    hdr->resizeSection(ModListModel::ColPriority, 40);
    hdr->resizeSection(ModListModel::ColVersion,  80);
    hdr->resizeSection(ModListModel::ColFlags,    60);
    // The list is manually ordered (drag-reorder); there's no real sort(), so
    // clickable headers would only flip a lying sort indicator. Disable sorting.
    setSortingEnabled(false);
    header()->setSectionsClickable(false);
    // When the user resizes any OTHER column, the Name column absorbs the slack so
    // the columns keep filling the pane (no gap). Resizing Name itself is left alone.
    connect(header(), &QHeaderView::sectionResized, this, [this](int idx, int, int) {
        if (idx != ModListModel::ColName) fillNameColumn();
    });
    // Right-click the header to toggle column visibility (persisted in AppConfig).
    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header(), &QWidget::customContextMenuRequested,
            this, &ModListView::showHeaderMenu);
    applyHiddenColumns(); // restore persisted hidden columns on startup

    connect(selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection&, const QItemSelection&) {
        QStringList ids;
        const auto rows = selectionModel()->selectedRows();
        for (const auto& idx : rows) {
            const auto* entry = m_model->entryAt(idx.row());
            if (!entry)
                ids << "__overwrite__";
            else if (entry->type == EntryType::Separator)
                ids << "__separator__";
            else
                ids << entry->id;
        }
        emit modsSelected(ids);
        updateConflictHighlights();
    });
}

void ModListView::setProfile(Profile* profile) {
    m_model->setProfile(profile);
    if (profile) {
        auto warns = DependencyChecker::check(profile->modList(),
                        AppConfig::instance().stagingDir());
        m_model->setDependencyWarnings(warns);
    }
    applyRowSpans(); // separators span full width; reset stale spans
    applyFilter(); // model rebuilt -> re-apply any active filter
    // Re-fit columns once the profile's data first populates. The viewport-width
    // guard in autoSizeColumns() makes this a no-op if the view isn't shown yet.
    QTimer::singleShot(0, this, [this]{ autoSizeColumns(); });
}

void ModListView::selectModById(const QString& id) {
    Profile* profile = m_model->profile();
    if (!profile || id.isEmpty()) return;
    const ModList& ml = profile->modList();
    int raw = -1;
    for (int i = 0; i < ml.count(); ++i)
        if (ml.at(i).id == id) { raw = i; break; }
    if (raw < 0) return;
    const int row = m_model->rawToVisible(raw);
    if (row < 0) return; // hidden (e.g. inside a collapsed group/separator)
    const QModelIndex idx = m_model->index(row, ModListModel::ColName);
    if (!idx.isValid()) return;
    selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    scrollTo(idx, QAbstractItemView::PositionAtCenter);
    setFocus();
}

void ModListView::autoSizeColumns() {
    // Fit every column to header + cell contents (leaves sections Interactive),
    // then let the Name column consume the remaining width.
    header()->resizeSections(QHeaderView::ResizeToContents);
    fillNameColumn();
}

// Resize the Name column so the columns always span the full viewport (no empty
// gap on the right, no Stretch section so manual resizes stay intuitive).
void ModListView::fillNameColumn() {
    const int vw = viewport()->width();
    if (vw <= 0) return; // not laid out yet
    int other = 0;
    for (int c = 0; c < m_model->columnCount(); ++c)
        if (c != ModListModel::ColName) other += header()->sectionSize(c);
    const int target = qMax(160, vw - other);
    if (target != header()->sectionSize(ModListModel::ColName))
        header()->resizeSection(ModListModel::ColName, target);
}

void ModListView::resizeEvent(QResizeEvent* event) {
    QTreeView::resizeEvent(event);
    fillNameColumn(); // refill on pane resize so columns never under-fill the width
}

void ModListView::applyRowSpans() {
    // Separator rows render full-width with their content in column 0; every other
    // row spans normally. Resets clear spans, so this re-applies (and resets stale
    // spans on rows that are no longer separators).
    const int rows = m_model->rowCount();
    for (int row = 0; row < rows; ++row) {
        const auto* entry = m_model->entryAt(row);
        const bool isSep = entry && entry->type == EntryType::Separator;
        setFirstColumnSpanned(row, QModelIndex(), isSep);
    }
}

void ModListView::showEvent(QShowEvent* event) {
    QTreeView::showEvent(event);
    if (!m_didAutoSize) {
        m_didAutoSize = true;
        QTimer::singleShot(0, this, [this]{ autoSizeColumns(); });
    }
}

void ModListView::paintEvent(QPaintEvent* event) {
    QTreeView::paintEvent(event);
    // When the list holds only the Overwrite row (a fresh profile), draw a
    // centered hint over the viewport so the empty list isn't bewildering.
    if (model() && model()->rowCount() <= 1) {
        QPainter painter(viewport());
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        const QRect r = viewport()->rect().adjusted(40, 40, -40, -40);
        painter.drawText(r, Qt::AlignCenter | Qt::TextWordWrap,
            QStringLiteral("No mods yet.\n\nAdd mods via the Downloads tab, "
                           "\xe2\x86\x92 Browse Nexus, or Install Wabbajack "
                           "Modlist\xe2\x80\xa6 (\xe2\x9a\x99 menu)."));
    }
}

void ModListView::invalidateModCache(const QString& id) {
    m_model->invalidateModCache(id);
}

void ModListView::setUpdateInfo(const QHash<QString, QPair<QString,QString>>& info) {
    m_model->setUpdateInfo(info);
    applyFilter(); // the "Update available" state filter depends on this data
}

void ModListView::setConflictIndex(const ConflictIndex& index) {
    m_conflicts = index;
    m_model->setConflictIndex(index); // always-on Flags-column winner/loser icons
    updateConflictHighlights();
    applyFilter(); // the "Has conflicts" state filter depends on this data
}

void ModListView::refreshFlags() {
    m_model->refreshFlags();
}

void ModListView::updateConflictHighlights() {
    // Highlight relative to a single selected mod (MO2 behaviour). Anything else
    // (none / multiple / a separator / Overwrite) clears the highlight.
    const auto ids = selectedModIds();
    if (ids.size() != 1) { m_model->setConflictHighlights({}); return; }
    const QString sel = ids.first();

    QHash<QString,int> hi; // modId -> 1 green (overwrites sel) / 2 red (overwritten by sel)
    // Files the selected mod LOSES -> their winner overwrites it -> GREEN.
    for (const QString& f : m_conflicts.losingFilesOf(sel)) {
        const QString w = m_conflicts.winnerOf(f);
        if (!w.isEmpty() && w != sel) hi[w] = 1;
    }
    // Files the selected mod WINS -> its losers are overwritten by it -> RED.
    for (const QString& f : m_conflicts.winningFilesOf(sel))
        for (const QString& l : m_conflicts.losersOf(f))
            if (l != sel && !hi.contains(l)) hi[l] = 2; // green takes precedence
    m_model->setConflictHighlights(hi);
}

void ModListView::mouseDoubleClickEvent(QMouseEvent* event) {
    auto idx = indexAt(event->pos());
    if (!idx.isValid()) { QTreeView::mouseDoubleClickEvent(event); return; }
    const auto* entry = m_model->entryAt(idx.row());
    if (entry && entry->type == EntryType::Separator) {
        m_model->toggleCollapse(idx.row());
        return;
    }
    // A group-parent mod toggles collapse instead of activating Data.
    if (entry && entry->type == EntryType::Mod
            && m_model->isGroupParent(m_model->rawIndexForRow(idx.row()))) {
        m_model->toggleModCollapse(idx.row());
        return;
    }
    // Mod (or Overwrite): activate -> right pane shows its Data.
    emit modActivated(entry ? entry->id : QString("__overwrite__"));
    QTreeView::mouseDoubleClickEvent(event);
}

void ModListView::mousePressEvent(QMouseEvent* event) {
    auto idx = indexAt(event->pos());
    if (idx.isValid()) {
        const auto* entry = m_model->entryAt(idx.row());
        if (entry && entry->type == EntryType::Separator) {
            // A separator renders full-width in the spanned column 0; its content
            // is laid out left-to-right as: [optional icon] [▶/▼ arrow] [name].
            // Derive the icon/text rects from the actual style geometry rather than
            // guessing offsets (which ignored the tree indentation + item padding).
            const QPoint pos = event->pos();
            QModelIndex spanIdx = m_model->index(idx.row(), ModListModel::ColEnabled);
            QStyleOptionViewItem opt;
            initViewItemOption(&opt);
            opt.rect = visualRect(spanIdx);
            opt.index = spanIdx;
            opt.features |= QStyleOptionViewItem::HasDisplay;
            const bool hasIcon = !entry->icon.isEmpty();
            if (hasIcon) {
                opt.features |= QStyleOptionViewItem::HasDecoration;
                opt.icon = spanIdx.data(Qt::DecorationRole).value<QIcon>();
                opt.decorationSize = iconSize();
            }
            // Icon region -> picker (only meaningful when there is an icon).
            QRect iconRect = hasIcon
                ? style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &opt, this)
                : QRect();
            // Text region: the arrow glyph is the leading run of the displayed text.
            QRect textRect = style()->subElementRect(QStyle::SE_ItemViewItemText, &opt, this);
            QFont f = font(); f.setBold(true); // separators render bold (see model)
            const QFontMetrics fm(f);
            // The displayed label is "<indent><arrow>  <name>" (see ModListModel):
            // 4 leading spaces per nesting level. Offset the arrow hit region by that
            // indent width so the click target tracks the visually-inset glyph.
            const int indentW = fm.horizontalAdvance(QString(entry->separatorLevel * 4, QChar(' ')));
            int arrowW = fm.horizontalAdvance(QStringLiteral("\xe2\x96\xbc  ")) + 4;
            QRect arrowRect(textRect.left() + indentW, opt.rect.top(), arrowW, opt.rect.height());
            // Order matters: icon picker first, then the disclosure arrow, then fall
            // through to the base handler (preserving selection + drag-to-reorder).
            if (hasIcon && iconRect.contains(pos)) {
                showIconPicker(idx.row(), event->globalPosition().toPoint());
                return; // consume
            }
            if (arrowRect.contains(pos)) {
                m_model->toggleCollapse(idx.row());
                return; // consume
            }
        }
    }
    QTreeView::mousePressEvent(event);
}

void ModListView::showIconPicker(int row, const QPoint& gpos) {
    const auto* entry = m_model->entryAt(row);
    if (!entry || entry->type != EntryType::Separator || !m_model->profile()) return;
    QString id = entry->id;
    QMenu menu(this);
    auto* grid = new QListWidget(&menu);
    grid->setViewMode(QListView::IconMode);
    grid->setIconSize(QSize(26,26));
    grid->setGridSize(QSize(38,38));
    grid->setUniformItemSizes(true);
    grid->setMovement(QListView::Static);
    grid->setResizeMode(QListView::Adjust);
    grid->setSpacing(2);
    grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid->setStyleSheet("background:#2b2b2b; border:none;");
    grid->setFrameShape(QFrame::NoFrame);
    // "None" first
    auto* none = new QListWidgetItem(grid); none->setData(Qt::UserRole, QString()); none->setToolTip("None");
    none->setIcon(solero::redCrossIcon(26));
    none->setSizeHint(QSize(34,34));
    for (const QString& f : QDir(":/icons/separators").entryList(QStringList()<<"*.svg", QDir::Files)) {
        QString path = ":/icons/separators/" + f;
        auto* it = new QListWidgetItem(grid);
        it->setIcon(solero::renderSvgIcon(path, Qt::white, 26));
        it->setData(Qt::UserRole, path);
        it->setToolTip(QFileInfo(f).completeBaseName());
        it->setSizeHint(QSize(34,34));
    }
    // ~6 columns
    grid->setFixedSize(6*38 + 16, 5*38 + 8);
    auto* wa = new QWidgetAction(&menu);
    wa->setDefaultWidget(grid);
    menu.addAction(wa);
    connect(grid, &QListWidget::itemClicked, this, [this, id, &menu](QListWidgetItem* it){
        if (auto* e = m_model->profile() ? m_model->profile()->modList().findById(id) : nullptr) {
            ModEntry up = *e; up.icon = it->data(Qt::UserRole).toString();
            m_model->profile()->modList().update(id, up);
            m_model->profile()->save();
            m_model->rebuild();
        }
        menu.close();
    });
    menu.exec(gpos);
}

void ModListView::contextMenuEvent(QContextMenuEvent* event) {
    auto idx = indexAt(event->pos());
    QMenu menu(this);

    if (!idx.isValid()) {
        // Right-click on empty space
        menu.addAction("Add Separator", [this]{ onAddSeparator(); });
        menu.exec(event->globalPos());
        return;
    }

    const auto* entry = m_model->entryAt(idx.row());
    if (!entry) {
        // Overwrite row (per-profile capture dir)
        auto* owProfile = m_model->profile();
        const QString ow = AppConfig::overwriteDir(owProfile ? owProfile->name() : QString());
        // Enabled only when the overwrite dir actually holds files.
        bool hasFiles = false;
        {
            QDirIterator it(ow, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                            QDirIterator::Subdirectories);
            hasFiles = it.hasNext();
        }
        QAction* createAct = menu.addAction(
            QStringLiteral("Create Mod from Overwrite") + QChar(0x2026),
            [this]{ emit createModFromOverwriteRequested(); });
        createAct->setEnabled(hasFiles);
        menu.addAction("Open Overwrite Folder", [ow]{
            QDir().mkpath(ow);
            QDesktopServices::openUrl(QUrl::fromLocalFile(ow));
        });
    } else if (entry->type == EntryType::Separator) {
        menu.addAction("Edit Separator", [this, row = idx.row()]{ onEditSeparator(row); });
        menu.addAction(entry->collapsed ? "Expand" : "Collapse",
                       [this, row = idx.row()]{ m_model->toggleCollapse(row); });
        menu.addAction("Delete Separator", [this, row = idx.row()]{ onDeleteSeparator(row); });
        menu.addSeparator();
        // Nest / un-nest into sub-categories. Indent is disabled when it would jump
        // past (nearest preceding separator level + 1); promote is disabled at top.
        {
            const int raw = m_model->rawIndexForRow(idx.row());
            const auto& list = m_model->profile()->modList();
            int prevLevel = -1;
            for (int i = raw - 1; i >= 0; --i)
                if (list.at(i).type == EntryType::Separator) {
                    prevLevel = list.at(i).separatorLevel; break;
                }
            QAction* indentAct = menu.addAction("Make sub-category (indent)",
                [this, row = idx.row()]{ onIndentSeparator(row); });
            indentAct->setEnabled(entry->separatorLevel < prevLevel + 1);
            QAction* promoteAct = menu.addAction("Promote (outdent)",
                [this, row = idx.row()]{ onOutdentSeparator(row); });
            promoteAct->setEnabled(entry->separatorLevel > 0);
        }
        menu.addSeparator();
        menu.addAction("Add Separator Below", [this, row = idx.row()]{ onAddSeparatorAt(row + 1); });
    } else {
        // If the right-clicked mod isn't part of the current selection, make it
        // the sole selection so delete/operations act on what the user clicked.
        if (!selectionModel()->isRowSelected(idx.row(), idx.parent())) {
            selectionModel()->select(
                m_model->index(idx.row(), 0),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        menu.addAction("Open in File Manager", [this, id = entry->id]{
            const QString folder = m_model->profile() ? m_model->profile()->stagingFolderFor(id) : id;
            QString dir = AppConfig::instance().stagingDir() + "/" + folder;
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        menu.addAction(entry->hasFomodChoices ? "Reinstall (FOMOD)..." : "Reinstall...",
                       [this, id = entry->id]{ emit reinstallRequested(id); });
        if (!entry->nexusModId.isEmpty()) {
            menu.addAction("Update Mod",
                           [this, id = entry->id]{ emit updateRequested(id); });
            if (!entry->nexusFileId.isEmpty())
                menu.addAction("Redownload from Nexus",
                               [this, id = entry->id]{ emit redownloadRequested(id); });
            menu.addAction("Endorse on Nexus",
                           [this, id = entry->id]{ emit endorseRequested(id); });
        }
        menu.addAction("Identify on Nexus (MD5)",
                       [this, id = entry->id]{ emit identifyRequested(id); });
        menu.addAction("Delete Mod...", [this]{ deleteSelectedMods(); });
        menu.addAction("Rename", [this, row = idx.row()]{ edit(m_model->index(row, ModListModel::ColName)); });
        if (m_model->isGroupParent(m_model->rawIndexForRow(idx.row()))) {
            menu.addSeparator();
            menu.addAction(entry->collapsed ? "Expand group" : "Collapse group",
                           [this, row = idx.row()]{ m_model->toggleModCollapse(row); });
        }
        // Group / Ungroup actions.
        {
            int raw = m_model->rawIndexForRow(idx.row());
            QStringList selModIds = selectedModIds();
            if (selModIds.size() >= 2) {
                menu.addSeparator();
                menu.addAction("Group selected mods", [this]{ groupSelectedMods(); });
            }
            if (m_model->isGroupChild(raw)) {
                menu.addSeparator();
                menu.addAction("Ungroup", [this, id = entry->id]{ ungroupMod(id); });
            }
        }
        menu.addSeparator();
        menu.addAction("Enable selected",  [this]{ setSelectedModsEnabled(true); });
        menu.addAction("Disable selected", [this]{ setSelectedModsEnabled(false); });
        // Community Shaders base mod: offer to wipe its compiled shader cache.
        if (!entry->isManagedCache
            && (entry->nexusModId == "86492"
                || entry->name.compare("Community Shaders", Qt::CaseInsensitive) == 0)) {
            menu.addSeparator();
            menu.addAction("Clear Shader Cache",
                           [this, id = entry->id]{ emit clearShaderCacheRequested(id); });
        }
        menu.addSeparator();
        menu.addAction("Add Separator Above", [this, row = idx.row()]{ onAddSeparatorAt(row); });
    }
    menu.exec(event->globalPos());
}

void ModListView::onAddSeparator() {
    onAddSeparatorAt(m_model->rowCount() - 1); // before Overwrite
}

void ModListView::onAddSeparatorAt(int visibleRow) {
    if (!m_model->profile()) return;
    bool ok;
    QString name = QInputDialog::getText(this, "New Separator", "Separator name:", QLineEdit::Normal, "New Category", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    ModEntry sep;
    sep.type = EntryType::Separator;
    sep.id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    sep.name = name.trimmed();
    static const QStringList kPalette = {"#c0392b","#d35400","#f39c12","#27ae60","#16a085","#2980b9","#8e44ad","#7f8c8d","#2c3e50","#c2185b"};
    QString last = AppConfig::instance().lastSeparatorColor();
    QString chosen;
    if (AppConfig::instance().cycleSeparatorColors()) {
        int i = kPalette.indexOf(last);
        chosen = kPalette.at((i + 1) % kPalette.size());  // i==-1 -> index 0
    } else {
        chosen = last.isEmpty() ? kPalette.first() : last;
    }
    sep.color = chosen;
    AppConfig::instance().setLastSeparatorColor(chosen);
    AppConfig::instance().save();

    // Insert at the given visible row position in the raw list. The Overwrite row
    // maps to raw -1; "append to the end" must land ABOVE the trailing
    // managed-cache mod (which stays pinned last), not at count().
    int rawPos = m_model->rawIndexForRow(visibleRow);
    if (rawPos < 0) rawPos = m_model->profile()->modList().firstTrailingManagedCacheIndex();

    // Append then move to position
    const QString sepId = sep.id;
    m_model->profile()->modList().append(sep);
    int newRaw = m_model->profile()->modList().count() - 1;
    if (rawPos < newRaw)
        m_model->profile()->modList().move(newRaw, rawPos);
    m_model->profile()->save();
    m_model->rebuild();

    // Open edit dialog immediately so user can pick colour/icon. Resolve the
    // separator's row by its id after the rebuild - rawPos may no longer point at
    // it once managed-cache pinning / other entries shift things around.
    const auto& entries = m_model->profile()->modList().entries();
    int sepRaw = -1;
    for (int i = 0; i < entries.size(); ++i)
        if (entries.at(i).id == sepId) { sepRaw = i; break; }
    if (sepRaw >= 0)
        onEditSeparator(m_model->rawToVisible(sepRaw));
}

void ModListView::onEditSeparator(int visibleRow) {
    const auto* entry = m_model->entryAt(visibleRow);
    if (!entry || entry->type != EntryType::Separator) return;
    SeparatorDialog dlg(*entry, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_model->profile()->modList().update(entry->id, dlg.result());
        m_model->profile()->save();
        m_model->rebuild();
    }
}

void ModListView::onDeleteSeparator(int visibleRow) {
    const auto* entry = m_model->entryAt(visibleRow);
    if (!entry || entry->type != EntryType::Separator) return;
    QString id = entry->id;
    QString name = entry->name;
    if (QMessageBox::question(this, "Delete Separator",
            QString("Delete separator \"%1\"? (Mods under it are not deleted.)").arg(name))
        != QMessageBox::Yes) return;
    m_model->profile()->modList().remove(id);
    m_model->profile()->save();
    m_model->rebuild();
}

void ModListView::onIndentSeparator(int visibleRow) {
    const auto* entry = m_model->entryAt(visibleRow);
    if (!entry || entry->type != EntryType::Separator || !m_model->profile()) return;
    auto& list = m_model->profile()->modList();
    const int raw = m_model->rawIndexForRow(visibleRow);
    // Clamp to (nearest preceding separator level + 1) so depth can't skip levels.
    // No preceding separator -> prevLevel -1 -> maxLevel 0 -> can't indent (stays 0).
    int prevLevel = -1;
    for (int i = raw - 1; i >= 0; --i)
        if (list.at(i).type == EntryType::Separator) { prevLevel = list.at(i).separatorLevel; break; }
    const int maxLevel = prevLevel + 1;
    ModEntry up = *entry;
    up.separatorLevel = qMin(up.separatorLevel + 1, maxLevel);
    if (up.separatorLevel == entry->separatorLevel) return; // no-op (already at max)
    list.update(entry->id, up);
    m_model->profile()->save();
    m_model->rebuild();
}

void ModListView::onOutdentSeparator(int visibleRow) {
    const auto* entry = m_model->entryAt(visibleRow);
    if (!entry || entry->type != EntryType::Separator || !m_model->profile()) return;
    if (entry->separatorLevel <= 0) return;
    ModEntry up = *entry;
    up.separatorLevel = up.separatorLevel - 1;
    m_model->profile()->modList().update(entry->id, up);
    m_model->profile()->save();
    m_model->rebuild();
}

void ModListView::deleteSelectedMods() {
    if (!m_model->profile()) return;

    // Gather the selected mod entries (skip separators / Overwrite).
    QStringList ids, names;
    const auto rows = selectionModel()->selectedRows();
    for (const auto& idx : rows) {
        const auto* entry = m_model->entryAt(idx.row());
        if (!entry || entry->type != EntryType::Mod) continue;
        ids   << entry->id;
        names << entry->name;
    }
    if (ids.isEmpty()) return;

    if (AppConfig::instance().confirmModDeletion()) {
        QString text = QString("Delete %1 mod(s)? This removes them from the list "
                               "and deletes their staged files. This cannot be undone.")
                           .arg(ids.size());
        if (ids.size() <= 5)
            text += "\n\n\xe2\x80\xa2 " + names.join("\n\xe2\x80\xa2 ");
        QMessageBox box(QMessageBox::Question, "Delete Mod", text,
                        QMessageBox::Yes | QMessageBox::No, this);
        auto* dontAsk = new QCheckBox("Don't ask me again", &box);
        box.setCheckBox(dontAsk);
        if (box.exec() != QMessageBox::Yes) return;
        if (dontAsk->isChecked()) {
            AppConfig::instance().setConfirmModDeletion(false);
            AppConfig::instance().save();
        }
    }

    const QString stagingDir = AppConfig::instance().stagingDir();
    for (const QString& id : ids) {
        // Resolve the on-disk folder (name-based after migration) before removing
        // the entry, then delete it.
        const QString folder = m_model->profile() ? m_model->profile()->stagingFolderFor(id) : id;
        QDir(stagingDir + "/" + folder).removeRecursively();
        m_model->profile()->modList().remove(id);
        m_model->invalidateModCache(id); // its staged files are now gone
    }
    m_model->profile()->save();
    m_model->rebuild();
    emit modsChanged();
}

void ModListView::setFilter(const QString& text) {
    m_filter = text.trimmed();
    applyFilter();
}

void ModListView::setStateFilter(StateFilter state) {
    m_stateFilter = state;
    applyFilter();
}

bool ModListView::matchesState(const ModEntry* entry) const {
    if (!entry || entry->type != EntryType::Mod) return true;
    switch (m_stateFilter) {
        case StateFilter::All:             return true;
        case StateFilter::Conflicts:       return m_model->modHasConflict(entry->id);
        case StateFilter::UpdateAvailable: return m_model->modHasUpdate(entry->id);
        case StateFilter::Enabled:         return entry->enabled;
        case StateFilter::Disabled:        return !entry->enabled;
        case StateFilter::MissingDep:      return m_model->modHasMissingDep(entry->id);
    }
    return true;
}

void ModListView::applyFilter() {
    // Hide Mod rows that don't match the name filter and the state predicate.
    // While any filter is active, separators whose section has no visible mod are
    // hidden too (no empty sections). The Overwrite row always stays visible.
    // rebuild() clears hidden state, so callers that rebuild re-apply the filter.
    const QModelIndex root;
    const bool filtering = !m_filter.isEmpty() || m_stateFilter != StateFilter::All;
    // Reveal collapsed-section mods while filtering so they become candidate rows;
    // restores normal collapse when the filter clears (early-returns if unchanged).
    m_model->setSearchExpandAll(filtering);
    const int rows = m_model->rowCount();

    QList<bool> hidden(rows, false);
    for (int row = 0; row < rows; ++row) {
        const auto* entry = m_model->entryAt(row);
        if (!entry || entry->type != EntryType::Mod) continue; // sep/Overwrite below
        bool hide = false;
        if (!m_filter.isEmpty())
            hide = !entry->name.contains(m_filter, Qt::CaseInsensitive);
        if (!hide && !matchesState(entry))
            hide = true;
        hidden[row] = hide;
    }
    if (filtering) {
        // Hide a separator if no mod in its section (up to the next separator or
        // the Overwrite row) is visible.
        for (int row = 0; row < rows; ++row) {
            const auto* entry = m_model->entryAt(row);
            if (!entry || entry->type != EntryType::Separator) continue;
            bool anyVisible = false;
            for (int r = row + 1; r < rows; ++r) {
                const auto* e2 = m_model->entryAt(r);
                if (!e2 || e2->type == EntryType::Separator) break; // Overwrite / next sep
                if (!hidden[r]) { anyVisible = true; break; }
            }
            hidden[row] = !anyVisible;
        }
    }
    for (int row = 0; row < rows; ++row)
        setRowHidden(row, root, hidden[row]);
}

void ModListView::applyHiddenColumns() {
    // ColName stays mandatory; restore the rest from AppConfig.
    const auto hiddenCols = AppConfig::instance().hiddenColumns();
    for (int c = 0; c < ModListModel::ColCount; ++c) {
        if (c == ModListModel::ColName) { setColumnHidden(c, false); continue; }
        setColumnHidden(c, hiddenCols.contains(c));
    }
}

void ModListView::showHeaderMenu(const QPoint& pos) {
    QMenu menu(this);
    struct ColInfo { int col; const char* label; };
    static const ColInfo cols[] = {
        { ModListModel::ColEnabled,  "Enabled" },
        { ModListModel::ColPriority, "Priority (#)" },
        { ModListModel::ColVersion,  "Version" },
        { ModListModel::ColFlags,    "Flags" },
    };
    for (const auto& ci : cols) {
        QAction* a = menu.addAction(QString::fromLatin1(ci.label));
        a->setCheckable(true);
        a->setChecked(!isColumnHidden(ci.col));
        connect(a, &QAction::toggled, this, [this, col = ci.col](bool shown) {
            setColumnHidden(col, !shown);
            QList<int> hidden;
            for (int c = 0; c < ModListModel::ColCount; ++c)
                if (c != ModListModel::ColName && isColumnHidden(c)) hidden << c;
            AppConfig::instance().setHiddenColumns(hidden);
            AppConfig::instance().save();
            fillNameColumn(); // Name absorbs the freed/used width
        });
    }
    menu.exec(header()->mapToGlobal(pos));
}

void ModListView::setSelectedModsEnabled(bool enabled) {
    if (!m_model->profile()) return;
    QStringList ids;
    const auto rows = selectionModel()->selectedRows();
    for (const auto& idx : rows) {
        const auto* entry = m_model->entryAt(idx.row());
        if (entry && entry->type == EntryType::Mod) ids << entry->id;
    }
    if (ids.isEmpty()) return;
    for (const QString& id : ids)
        m_model->profile()->modList().setEnabled(id, enabled);
    m_model->profile()->save();
    m_model->rebuild();
    applyFilter();
    emit modsChanged();
}

QStringList ModListView::selectedModIds() const {
    // Selected mod rows in LIST (raw) order; separators / Overwrite excluded.
    QList<QPair<int,QString>> picked;
    const auto rows = selectionModel()->selectedRows();
    for (const auto& idx : rows) {
        const auto* entry = m_model->entryAt(idx.row());
        if (!entry || entry->type != EntryType::Mod) continue;
        picked.append({m_model->rawIndexForRow(idx.row()), entry->id});
    }
    std::sort(picked.begin(), picked.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });
    QStringList ids;
    for (const auto& p : picked) ids << p.second;
    return ids;
}

void ModListView::groupSelectedMods() {
    if (!m_model->profile()) return;
    QStringList ids = selectedModIds();
    if (ids.size() < 2) return;
    // First (topmost in list order) becomes the parent; the rest are nested under
    // it contiguously, in their selection order, via groupUnder.
    const QString parentId = ids.first();
    auto& list = m_model->profile()->modList();
    for (int i = 1; i < ids.size(); ++i)
        list.groupUnder(ids.at(i), parentId);
    m_model->profile()->save();
    m_model->rebuild();
    applyFilter();
    emit modsChanged();
}

void ModListView::ungroupMod(const QString& id) {
    if (!m_model->profile()) return;
    m_model->profile()->modList().ungroup(id);
    m_model->profile()->save();
    m_model->rebuild();
    applyFilter();
    emit modsChanged();
}

void ModListView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        deleteSelectedMods();
        return;
    }
    QTreeView::keyPressEvent(event);
}

} // namespace solero
