#include "BottomPanel.h"
#include "ModInfoWidget.h"
#include "core/AppConfig.h"
#include "app/Application.h"
#include "ui/IconUtil.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QApplication>

namespace solero {

BottomPanel::BottomPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Always-visible header bar: collapse arrow + "Mod Info" label.
    auto* header = new QWidget(this);
    m_header = header;
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(4);

    m_collapseBtn = new QToolButton(header);
    m_collapseBtn->setAutoRaise(true);
    m_collapseBtn->setToolTip("Show/Hide mod info");
    headerLayout->addWidget(m_collapseBtn);

    // Rich-text bold so it stays bold while still inheriting the app font (a
    // stylesheet or explicit font would freeze at the construction-time size and
    // not track Ctrl +/- zoom).
    auto* title = new QLabel("<b>Mod Info</b>", header);
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    // Right-justified, dimmed inline progress for the update check. Hidden when idle
    // so it never competes with the "Mod Info" label. Lives here (not the status bar)
    // so a long check reports where the user is already looking.
    m_updateProgress = new QLabel(header);
    m_updateProgress->setStyleSheet("color: palette(mid);");
    // Ignored horizontal policy: the label may not force the header row wider than
    // the window (at high zoom its text otherwise clips off the right edge); the
    // layout shrinks it and the text elides via setUpdateProgress instead.
    m_updateProgress->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_updateProgress->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_updateProgress->hide();
    headerLayout->addWidget(m_updateProgress, 1);

    outer->addWidget(header);

    m_modInfo = new ModInfoWidget(this);
    connect(m_modInfo, &ModInfoWidget::noteChanged, this, &BottomPanel::noteChanged);
    outer->addWidget(m_modInfo);

    connect(m_collapseBtn, &QToolButton::clicked, this, [this]{
        bool expanded = !m_modInfo->isVisible();
        setExpanded(expanded);
        solero::AppConfig::instance().setInfoPanelVisible(expanded);
        solero::AppConfig::instance().save();
    });

    // Re-evaluate the panel's height caps on zoom: the collapsed cap tracks the
    // (now larger) header, and the expanded cap scales so the mod-info content
    // isn't clipped at high zoom.
    connect(static_cast<Application*>(qApp), &Application::zoomChanged, this,
            [this]{ setExpanded(m_expanded); });

    // Apply persisted state (default true -> expanded).
    setExpanded(solero::AppConfig::instance().infoPanelVisible());
}

void BottomPanel::setExpanded(bool expanded) {
    m_expanded = expanded;
    m_modInfo->setVisible(expanded);
    if (expanded) {
        m_collapseBtn->setText("\xe2\x96\xbc"); // ▼
        setMaximumHeight(solero::zoomScaledPx(250)); // scales with zoom
    } else {
        m_collapseBtn->setText("\xe2\x96\xb2"); // ▲
        // Live header height (not a construction-time cache) so the collapsed strip
        // grows with the font.
        setMaximumHeight(m_header ? m_header->sizeHint().height() : m_headerHeight);
    }
}

void BottomPanel::setUpdateProgress(int done, int total) {
    if (!m_updateProgress) return;
    if (total <= 0 || done < 0 || done >= total) {
        m_updateProgress->hide();
        m_updateProgress->clear();
        return;
    }
    const QString full = QStringLiteral("Checking updates") + QChar(0x2026)
                       + QStringLiteral(" %1/%2").arg(done).arg(total);
    // Elide from the LEFT if squeezed (Ignored h-policy above) so the live "N/M"
    // tail stays visible instead of the text clipping at the window edge.
    const int avail = qMax(60, m_updateProgress->width() - 8);
    m_updateProgress->setText(
        m_updateProgress->fontMetrics().elidedText(full, Qt::ElideLeft, avail));
    m_updateProgress->show();
}

void BottomPanel::setProfile(Profile* profile) {
    m_profile = profile;
    if (m_modInfo) m_modInfo->clear();
}

void BottomPanel::onModsSelected(const QStringList& ids) {
    if (m_modInfo)
        m_modInfo->showMod(m_profile, ids.isEmpty() ? QString() : ids.first());
}

} // namespace solero
