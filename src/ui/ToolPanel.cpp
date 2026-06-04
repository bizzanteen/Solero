#include "ToolPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
namespace solero {
ToolPanel::ToolPanel(const Executable& exe, QWidget* parent) : QWidget(parent), m_exe(exe) {
    auto* v = new QVBoxLayout(this);

    // Title row: icon (if any) + name.
    auto* titleRow = new QHBoxLayout;
    if (!exe.iconPath.isEmpty()) {
        QPixmap pm(exe.iconPath);
        if (!pm.isNull()) {
            auto* iconLbl = new QLabel(this);
            iconLbl->setFixedSize(48, 48);
            iconLbl->setScaledContents(true);
            iconLbl->setPixmap(pm);
            titleRow->addWidget(iconLbl);
        }
    }
    auto* title = new QLabel("<h2>" + exe.name.toHtmlEscaped() + "</h2>", this);
    titleRow->addWidget(title);
    titleRow->addStretch();
    v->addLayout(titleRow);

    v->addWidget(new QLabel("Binary: " + exe.binaryPath, this));
    v->addWidget(new QLabel(QString("Runtime: %1").arg(exe.runtime == RuntimeType::Proton ? "Proton" : "Native"), this));
    if (!exe.arguments.isEmpty()) v->addWidget(new QLabel("Arguments: " + exe.arguments, this));
    v->addStretch();
    auto* runBtn = new QPushButton("\xe2\x96\xb6  Run " + exe.name, this);
    runBtn->setMinimumHeight(40);
    connect(runBtn, &QPushButton::clicked, this, [this]{ emit runRequested(m_exe); });
    v->addWidget(runBtn);

    // Secondary action buttons (e.g. "Run TexGen").
    for (const auto& a : exe.extraActions) {
        auto* actBtn = new QPushButton("\xe2\x96\xb6  " + a.label, this);
        actBtn->setMinimumHeight(36);
        connect(actBtn, &QPushButton::clicked, this, [this, a]{
            Executable ax = m_exe;
            ax.binaryPath = a.binaryPath;
            ax.arguments = a.arguments;
            ax.outputModId = a.outputModId;
            ax.isCapturingOutput = !a.outputModId.isEmpty();
            ax.extraActions.clear();
            emit runRequested(ax);
        });
        v->addWidget(actBtn);
    }
    auto* row = new QHBoxLayout;
    auto* edit = new QPushButton("Edit\xe2\x80\xa6", this);
    auto* rm = new QPushButton("Remove", this);
    connect(edit, &QPushButton::clicked, this, [this]{ emit editRequested(m_exe.id); });
    connect(rm, &QPushButton::clicked, this, [this]{ emit removeRequested(m_exe.id); });
    row->addStretch(); row->addWidget(edit); row->addWidget(rm);
    v->addLayout(row);
}
}
