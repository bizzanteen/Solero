#include "ToolPanel.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
namespace solero {
ToolPanel::ToolPanel(const Executable& exe, QWidget* parent) : QWidget(parent), m_exe(exe) {
    auto* v = new QVBoxLayout(this);
    auto* title = new QLabel("<h2>" + exe.name.toHtmlEscaped() + "</h2>", this);
    v->addWidget(title);
    v->addWidget(new QLabel("Binary: " + exe.binaryPath, this));
    v->addWidget(new QLabel(QString("Runtime: %1").arg(exe.runtime == RuntimeType::Proton ? "Proton" : "Native"), this));
    if (!exe.arguments.isEmpty()) v->addWidget(new QLabel("Arguments: " + exe.arguments, this));
    v->addStretch();
    auto* runBtn = new QPushButton("\xe2\x96\xb6  Run " + exe.name, this);
    runBtn->setMinimumHeight(40);
    connect(runBtn, &QPushButton::clicked, this, [this]{ emit runRequested(m_exe); });
    v->addWidget(runBtn);
    auto* row = new QHBoxLayout;
    auto* edit = new QPushButton("Edit\xe2\x80\xa6", this);
    auto* rm = new QPushButton("Remove", this);
    connect(edit, &QPushButton::clicked, this, [this]{ emit editRequested(m_exe.id); });
    connect(rm, &QPushButton::clicked, this, [this]{ emit removeRequested(m_exe.id); });
    row->addStretch(); row->addWidget(edit); row->addWidget(rm);
    v->addLayout(row);
}
}
