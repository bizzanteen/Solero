#include "PatchWizardDialog.h"
#include "ProgressModal.h"
#include "core/Profile.h"
#include "core/AppConfig.h"
#include "install/ModInstaller.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QTextOption>

namespace solero {

namespace {

// The app's existing positive/green signal colour (see IconUtil.h
// greenUpTriangleIcon, used for "this mod wins file conflicts"). Mid green:
// readable on both the dark and light themes - unlike a dark "#2e7d32".
constexpr const char* kReasonGreen = "#27ae60";

// Roles carried by tree items.
enum {
    CandidateRole = Qt::UserRole,     // child only: index into m_candidates
    SearchRole    = Qt::UserRole + 1, // lowercased text used by the filter box
};

// Delegate that renders an item's display text as rich text so a single row can
// mix the normal-coloured option name with the green reason. Keeps the row to one
// line and clips (elides) overly long content. The checkbox/branch indicator and
// the selection background are still drawn by the style.
class HtmlItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QWidget* w = opt.widget;
        QStyle* style = w ? w->style() : QApplication::style();

        const QString html = opt.text;
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);

        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, w);
        const QColor base = (opt.state & QStyle::State_Selected)
            ? opt.palette.color(QPalette::HighlightedText)
            : opt.palette.color(QPalette::Text);

        QTextDocument doc;
        doc.setDocumentMargin(0);
        doc.setDefaultStyleSheet(QStringLiteral("body{color:%1;}").arg(base.name()));
        QTextOption to;
        to.setWrapMode(QTextOption::NoWrap);
        doc.setDefaultTextOption(to);
        doc.setHtml(QStringLiteral("<body>") + html + QStringLiteral("</body>"));

        painter->save();
        painter->setClipRect(textRect);
        const qreal y = textRect.top() + (textRect.height() - doc.size().height()) / 2.0;
        painter->translate(textRect.left(), y);
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.clip = QRectF(0, 0, textRect.width(), textRect.height());
        doc.documentLayout()->draw(painter, ctx);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(qMax(s.height(), 22));
        return s;
    }
};

} // namespace

PatchWizardDialog::PatchWizardDialog(Profile* profile, QWidget* parent)
    : QDialog(parent), m_profile(profile) {
    setWindowTitle("Patch Wizard");
    resize(680, 560);

    auto* root = new QVBoxLayout(this);

    auto* intro = new QLabel(
        "Surfaces FOMOD patches that are now applicable to your current load order "
        "(a matching mod or plugin is present) but were never installed. Tick the "
        "ones to install.", this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    // Toolbar: select all / none + filter.
    auto* tools = new QHBoxLayout();
    m_selectAllBtn = new QPushButton("Select all", this);
    m_selectNoneBtn = new QPushButton("Select none", this);
    connect(m_selectAllBtn, &QPushButton::clicked, this, [this] { setAllChecked(true); });
    connect(m_selectNoneBtn, &QPushButton::clicked, this, [this] { setAllChecked(false); });
    tools->addWidget(m_selectAllBtn);
    tools->addWidget(m_selectNoneBtn);
    tools->addStretch(1);
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("Filter") + QChar(0x2026)); // ellipsis
    m_filter->setClearButtonEnabled(true);
    m_filter->setMaximumWidth(240);
    connect(m_filter, &QLineEdit::textChanged, this, &PatchWizardDialog::applyFilter);
    tools->addWidget(m_filter);
    root->addLayout(tools);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setUniformRowHeights(true);
    m_tree->setItemDelegate(new HtmlItemDelegate(m_tree));
    m_tree->setExpandsOnDoubleClick(true);
    connect(m_tree, &QTreeWidget::itemChanged, this, &PatchWizardDialog::onItemChanged);
    root->addWidget(m_tree, 1);

    m_empty = new QLabel("No applicable patches found.", this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->hide();
    root->addWidget(m_empty);

    auto* buttons = new QDialogButtonBox(this);
    m_installBtn = buttons->addButton("Install Selected", QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(m_installBtn, &QPushButton::clicked, this, &PatchWizardDialog::onInstallSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    runScan();
    buildTree();
}

void PatchWizardDialog::runScan() {
    ProgressModal prog(this, "Patch Wizard",
                       QStringLiteral("Scanning installed FOMOD mods") + QChar(0x2026));
    prog.show();
    prog.pump();
    const QString gameDir = AppConfig::instance().gameDir();
    const QString staging = AppConfig::instance().stagingDir();
    m_candidates = scanProfile(*m_profile, gameDir, staging, [&](const QString& name) {
        prog.setMessage("Scanning: " + name);
        prog.pump();
    });
    prog.close();
}

void PatchWizardDialog::buildTree() {
    if (m_candidates.isEmpty()) {
        m_tree->hide();
        m_filter->setEnabled(false);
        m_selectAllBtn->setEnabled(false);
        m_selectNoneBtn->setEnabled(false);
        m_empty->show();
        m_installBtn->setEnabled(false);
        return;
    }

    m_updating = true; // suppress propagation while populating

    QHash<QString, QTreeWidgetItem*> groups; // modId -> top-level item
    QStringList order;                        // preserve scan order

    for (int i = 0; i < m_candidates.size(); ++i) {
        const PatchCandidate& c = m_candidates[i];
        QTreeWidgetItem* parent = groups.value(c.modId);
        if (!parent) {
            parent = new QTreeWidgetItem(m_tree);
            parent->setData(0, SearchRole, c.modName.toLower());
            groups.insert(c.modId, parent);
            order << c.modId;
        }

        auto* child = new QTreeWidgetItem(parent);
        child->setData(0, CandidateRole, i);
        child->setData(0, SearchRole,
                       (c.modName + " " + c.optionName + " " + c.reason).toLower());

        // One-line rich text: option name + green reason inline.
        QString html = c.optionName.toHtmlEscaped();
        if (!c.reason.isEmpty()) {
            // name <em dash> reason, the dash built from QChar (never a \xNN
            // byte escape, which mojibakes inside a QStringLiteral).
            html += QStringLiteral(" <span style=\"color:%1;\">%2 %3</span>")
                        .arg(kReasonGreen, QString(QChar('-')), c.reason.toHtmlEscaped());
        }
        if (!c.installable) {
            html += QStringLiteral(" <i>(install needs the source archive)</i>");
        }
        child->setData(0, Qt::DisplayRole, html);

        // Verbose detail -> tooltip.
        QString tip = c.optionName;
        if (!c.optionDescription.isEmpty())
            tip += "\n\n" + c.optionDescription;
        QStringList paths;
        for (const FomodFile& f : c.files) {
            const QString d = f.destination.isEmpty()
                ? (f.isFolder ? QStringLiteral("Data/") : f.source) : f.destination;
            paths << (f.isFolder ? (d + "/*") : d);
        }
        if (!paths.isEmpty()) {
            const int shown = qMin(paths.size(), 30);
            const QString bullet = QString(QChar(0x2022)) + QStringLiteral(" "); // "• "
            tip += "\n\nInstalls:\n" + bullet + paths.mid(0, shown).join("\n" + bullet);
            if (paths.size() > shown)
                tip += "\n" + QString(QChar(0x2026)) // ellipsis
                     + QStringLiteral(" (+%1 more)").arg(paths.size() - shown);
        }
        if (!c.installable)
            tip += "\n\n(install needs the source archive)";
        child->setToolTip(0, tip);

        if (c.installable) {
            child->setFlags((child->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsAutoTristate);
            child->setCheckState(0, Qt::Checked);
        } else {
            // Detected only: an unchecked, non-checkable, disabled row.
            child->setFlags(child->flags() & ~Qt::ItemIsUserCheckable);
            child->setCheckState(0, Qt::Unchecked);
            child->setDisabled(true);
        }
    }

    // Finalise each group: label with patch count + parent check state.
    for (const QString& modId : order) {
        QTreeWidgetItem* parent = groups.value(modId);
        const int n = parent->childCount();
        const QString name = m_candidates[parent->child(0)->data(0, CandidateRole).toInt()].modName;
        parent->setData(0, Qt::DisplayRole,
            QStringLiteral("<b>%1</b> (%2 patch%3)")
                .arg(name.toHtmlEscaped()).arg(n).arg(n == 1 ? "" : "es"));

        bool hasCheckable = false;
        for (int j = 0; j < n; ++j)
            if (parent->child(j)->flags() & Qt::ItemIsUserCheckable) { hasCheckable = true; break; }
        if (hasCheckable)
            parent->setFlags(parent->flags() | Qt::ItemIsUserCheckable);
        else
            parent->setFlags(parent->flags() & ~Qt::ItemIsUserCheckable);
        updateParentState(parent);
    }

    m_tree->expandAll();
    m_updating = false;
    updateInstallEnabled();
}

// Recompute a top-level item's tri-state from its checkable children.
void PatchWizardDialog::updateParentState(QTreeWidgetItem* parent) {
    if (!parent || !(parent->flags() & Qt::ItemIsUserCheckable)) return;
    int checkable = 0, checked = 0;
    for (int i = 0; i < parent->childCount(); ++i) {
        QTreeWidgetItem* ch = parent->child(i);
        if (!(ch->flags() & Qt::ItemIsUserCheckable)) continue;
        ++checkable;
        if (ch->checkState(0) == Qt::Checked) ++checked;
    }
    Qt::CheckState st = (checked == 0) ? Qt::Unchecked
                       : (checked == checkable) ? Qt::Checked
                       : Qt::PartiallyChecked;
    if (parent->checkState(0) != st) parent->setCheckState(0, st);
}

void PatchWizardDialog::onItemChanged(QTreeWidgetItem* item, int column) {
    if (m_updating || column != 0) return;
    m_updating = true;
    if (!item->parent()) {
        // Top-level toggled: drive all checkable children to match.
        const Qt::CheckState st = item->checkState(0);
        if (st != Qt::PartiallyChecked) {
            for (int i = 0; i < item->childCount(); ++i) {
                QTreeWidgetItem* ch = item->child(i);
                if (ch->flags() & Qt::ItemIsUserCheckable)
                    ch->setCheckState(0, st);
            }
        }
    } else {
        updateParentState(item->parent());
    }
    m_updating = false;
    updateInstallEnabled();
}

void PatchWizardDialog::setAllChecked(bool checked) {
    m_updating = true;
    const Qt::CheckState st = checked ? Qt::Checked : Qt::Unchecked;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* parent = m_tree->topLevelItem(i);
        for (int j = 0; j < parent->childCount(); ++j) {
            QTreeWidgetItem* ch = parent->child(j);
            if (ch->flags() & Qt::ItemIsUserCheckable)
                ch->setCheckState(0, st);
        }
        updateParentState(parent);
    }
    m_updating = false;
    updateInstallEnabled();
}

void PatchWizardDialog::applyFilter(const QString& text) {
    const QString needle = text.trimmed().toLower();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* parent = m_tree->topLevelItem(i);
        const bool parentMatch =
            needle.isEmpty() || parent->data(0, SearchRole).toString().contains(needle);
        bool anyVisible = false;
        for (int j = 0; j < parent->childCount(); ++j) {
            QTreeWidgetItem* ch = parent->child(j);
            const bool match = parentMatch || needle.isEmpty()
                || ch->data(0, SearchRole).toString().contains(needle);
            ch->setHidden(!match);
            anyVisible = anyVisible || match;
        }
        parent->setHidden(!anyVisible);
    }
}

void PatchWizardDialog::updateInstallEnabled() {
    int checked = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* parent = m_tree->topLevelItem(i);
        for (int j = 0; j < parent->childCount(); ++j) {
            QTreeWidgetItem* ch = parent->child(j);
            if ((ch->flags() & Qt::ItemIsUserCheckable) && ch->checkState(0) == Qt::Checked)
                ++checked;
        }
    }
    m_installBtn->setEnabled(checked > 0);
}

void PatchWizardDialog::onInstallSelected() {
    // Gather every checked child across all mod groups.
    QList<int> selected;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* parent = m_tree->topLevelItem(i);
        for (int j = 0; j < parent->childCount(); ++j) {
            QTreeWidgetItem* ch = parent->child(j);
            if ((ch->flags() & Qt::ItemIsUserCheckable) && ch->checkState(0) == Qt::Checked)
                selected << ch->data(0, CandidateRole).toInt();
        }
    }

    QStringList changed;
    int installed = 0;
    ProgressModal prog(this, "Patch Wizard",
                       QStringLiteral("Installing patches") + QChar(0x2026));
    prog.show();
    prog.pump();
    const QString staging = AppConfig::instance().stagingDir();
    for (int idx : selected) {
        if (idx < 0 || idx >= m_candidates.size()) continue;
        const PatchCandidate& c = m_candidates[idx];
        if (!c.installable || c.sourceArchive.isEmpty()) continue;
        prog.setMessage("Installing: " + c.optionName);
        prog.pump();
        const QString modDir = staging + "/" + c.modId;
        if (ModInstaller::installOptionFiles(c.sourceArchive, modDir, c.files)) {
            ++installed;
            if (!changed.contains(c.modId)) changed.append(c.modId);
        }
    }
    prog.close();

    if (installed == 0) {
        QMessageBox::information(this, "Patch Wizard",
            "No patches were installed. Either no patches were selected, or the selected patches are not compatible with your current mod list.");
        return;
    }
    emit patchesInstalled(changed);
    QMessageBox::information(this, "Patch Wizard",
        QString("Installed %1 patch%2 into %3 mod%4. Re-deploy to apply.")
            .arg(installed).arg(installed == 1 ? "" : "es")
            .arg(changed.size()).arg(changed.size() == 1 ? "" : "s"));
    accept();
}

} // namespace solero
