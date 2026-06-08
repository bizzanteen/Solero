#include "ProblemsDialog.h"
#include "IconUtil.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QPainter>
#include <QFont>
#include <QPointF>
#include <QRectF>

namespace solero {

namespace {
// Severity badge for the panel rows. Error reuses the shared red "!" circle;
// warning is an amber "!" circle; info is a slate "i" circle.
QIcon severityIcon(HealthSeverity sev) {
    if (sev == HealthSeverity::Error) return redBangIcon(18);
    const int px = 18;
    QPixmap pm(px, px); pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor fill = sev == HealthSeverity::Warning ? QColor("#e67e22")
                                                       : QColor("#2980b9");
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    const double m = px * 0.10;
    p.drawEllipse(QRectF(m, m, px - 2 * m, px - 2 * m));
    QFont f;
    f.setPixelSize(int(px * 0.62));
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(0, 0, px, px), Qt::AlignCenter,
               sev == HealthSeverity::Warning ? QStringLiteral("!")
                                              : QStringLiteral("i"));
    return QIcon(pm);
}

const char* kModRole    = "modId";
const char* kPluginRole = "plugin";
} // namespace

ProblemsDialog::ProblemsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Problems"));
    setModal(false);
    resize(560, 460);

    auto* layout = new QVBoxLayout(this);

    m_summary = new QLabel(this);
    layout->addWidget(m_summary);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(1);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(false);
    m_tree->setWordWrap(true);
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) { activateItem(item); });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        const auto sel = m_tree->selectedItems();
        bool canGo = false;
        if (!sel.isEmpty()) {
            const QString modId  = sel.first()->data(0, Qt::UserRole).toString();
            const QString plugin = sel.first()->data(0, Qt::UserRole + 1).toString();
            canGo = !modId.isEmpty() || !plugin.isEmpty();
        }
        m_goToBtn->setEnabled(canGo);
    });

    auto* buttons = new QHBoxLayout;
    auto* rescanBtn = new QPushButton(QStringLiteral("Re-scan"), this);
    connect(rescanBtn, &QPushButton::clicked, this, &ProblemsDialog::rescanRequested);
    m_goToBtn = new QPushButton(QStringLiteral("Go to"), this);
    m_goToBtn->setEnabled(false);
    connect(m_goToBtn, &QPushButton::clicked, this, &ProblemsDialog::goToCurrent);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    buttons->addWidget(rescanBtn);
    buttons->addStretch(1);
    buttons->addWidget(m_goToBtn);
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);
    Q_UNUSED(kModRole); Q_UNUSED(kPluginRole);
}

void ProblemsDialog::setIssues(const QList<HealthIssue>& issues) {
    m_tree->clear();

    // Info-level notices aren't actionable problems, so the panel ignores them
    // (the indicator does the same in MainWindow::refreshHealthIndicator).
    int errors = 0, warnings = 0;
    for (const auto& i : issues) {
        switch (i.severity) {
            case HealthSeverity::Error:   ++errors;   break;
            case HealthSeverity::Warning: ++warnings; break;
            case HealthSeverity::Info:    break; // excluded from the panel
        }
    }

    if (errors == 0 && warnings == 0) {
        m_summary->setText(QStringLiteral("No problems found."));
        return;
    }
    m_summary->setText(QStringLiteral("%1 error(s), %2 warning(s).")
                           .arg(errors).arg(warnings));

    // Group headers in worst-first order; only add a group that has children.
    struct Group { HealthSeverity sev; QString title; QTreeWidgetItem* node = nullptr; };
    Group groups[] = {
        { HealthSeverity::Error,   QStringLiteral("Errors") },
        { HealthSeverity::Warning, QStringLiteral("Warnings") },
    };

    auto groupFor = [&](HealthSeverity sev) -> QTreeWidgetItem* {
        for (auto& g : groups) {
            if (g.sev != sev) continue;
            if (!g.node) {
                g.node = new QTreeWidgetItem(m_tree);
                g.node->setText(0, g.title);
                QFont f = g.node->font(0); f.setBold(true); g.node->setFont(0, f);
                g.node->setFirstColumnSpanned(true);
                g.node->setFlags(Qt::ItemIsEnabled); // not selectable
            }
            return g.node;
        }
        return nullptr;
    };

    for (const auto& issue : issues) {
        if (issue.severity == HealthSeverity::Info) continue; // not shown
        QTreeWidgetItem* parent = groupFor(issue.severity);
        auto* item = new QTreeWidgetItem(parent);
        item->setIcon(0, severityIcon(issue.severity));
        QString text = issue.title;
        if (!issue.detail.isEmpty())  text += QStringLiteral("\n") + issue.detail;
        if (!issue.fixHint.isEmpty()) text += QStringLiteral("\nFix: ") + issue.fixHint;
        item->setText(0, text);
        item->setToolTip(0, text);
        item->setData(0, Qt::UserRole,     issue.targetModId);
        item->setData(0, Qt::UserRole + 1, issue.targetPlugin);
    }
    m_tree->expandAll();
}

void ProblemsDialog::activateItem(QTreeWidgetItem* item) {
    if (!item) return;
    const QString modId  = item->data(0, Qt::UserRole).toString();
    const QString plugin = item->data(0, Qt::UserRole + 1).toString();
    if (!modId.isEmpty())       emit goToMod(modId);
    else if (!plugin.isEmpty()) emit goToPlugin(plugin);
}

void ProblemsDialog::goToCurrent() {
    const auto sel = m_tree->selectedItems();
    if (!sel.isEmpty()) activateItem(sel.first());
}

} // namespace solero
