#include "BottomPanel.h"
#include "ModInfoWidget.h"

namespace solero {

BottomPanel::BottomPanel(QWidget* parent) : QTabWidget(parent) {
    m_modInfo = new ModInfoWidget(this);
    addTab(m_modInfo, "Mod Info");

    setMaximumHeight(250);
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
