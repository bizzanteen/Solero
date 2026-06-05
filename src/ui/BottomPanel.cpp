#include "BottomPanel.h"
#include "ModInfoWidget.h"
#include "core/AppConfig.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>

namespace solero {

BottomPanel::BottomPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Always-visible header bar: collapse arrow + "Mod Info" label.
    auto* header = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(4, 2, 4, 2);
    headerLayout->setSpacing(4);

    m_collapseBtn = new QToolButton(header);
    m_collapseBtn->setAutoRaise(true);
    m_collapseBtn->setToolTip("Show/Hide mod info");
    headerLayout->addWidget(m_collapseBtn);

    auto* title = new QLabel("Mod Info", header);
    title->setStyleSheet("font-weight: bold;");
    headerLayout->addWidget(title);
    headerLayout->addStretch();

    outer->addWidget(header);

    m_modInfo = new ModInfoWidget(this);
    outer->addWidget(m_modInfo);

    m_headerHeight = header->sizeHint().height();

    connect(m_collapseBtn, &QToolButton::clicked, this, [this]{
        bool expanded = !m_modInfo->isVisible();
        setExpanded(expanded);
        solero::AppConfig::instance().setInfoPanelVisible(expanded);
        solero::AppConfig::instance().save();
    });

    // Apply persisted state (default true -> expanded).
    setExpanded(solero::AppConfig::instance().infoPanelVisible());
}

void BottomPanel::setExpanded(bool expanded) {
    m_modInfo->setVisible(expanded);
    if (expanded) {
        m_collapseBtn->setText("\xe2\x96\xbc"); // ▼
        setMaximumHeight(250);
    } else {
        m_collapseBtn->setText("\xe2\x96\xb2"); // ▲
        setMaximumHeight(m_headerHeight);
    }
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
