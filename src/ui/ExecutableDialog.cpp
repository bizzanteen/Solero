#include "ExecutableDialog.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QFileDialog>
#include <QDir>

namespace solero {

ExecutableDialog::ExecutableDialog(const Executable& exe, QWidget* parent)
    : QDialog(parent), m_result(exe) {
    setWindowTitle(exe.name.isEmpty() ? "Add Executable" : "Edit Executable");
    setMinimumWidth(480);
    auto* layout = new QFormLayout(this);

    m_nameEdit    = new QLineEdit(exe.name, this);
    m_argsEdit    = new QLineEdit(exe.arguments, this);
    m_deployCheck = new QCheckBox("Run on deployment", this);
    m_deployCheck->setToolTip("Automatically run this tool after a successful deploy.");
    m_primaryCheck= new QCheckBox("Set as primary launch target", this);
    m_deployCheck->setChecked(exe.runThroughDeployer);
    m_primaryCheck->setChecked(exe.isPrimary);

    auto* binRow = new QHBoxLayout;
    m_binaryEdit = new QLineEdit(exe.binaryPath, this);
    auto* browseBtn = new QPushButton("...", this);
    browseBtn->setFixedWidth(30);
    binRow->addWidget(m_binaryEdit);
    binRow->addWidget(browseBtn);

    m_runtimeCombo = new QComboBox(this);
    m_runtimeCombo->addItem("Native (Linux)");
    m_runtimeCombo->addItem("Proton (Windows)");
    m_runtimeCombo->setCurrentIndex(exe.runtime == RuntimeType::Proton ? 1 : 0);

    m_protonGroup = new QWidget(this);
    auto* protonLayout = new QFormLayout(m_protonGroup);
    protonLayout->setContentsMargins(0, 0, 0, 0);
    m_protonCombo = new QComboBox(m_protonGroup);
    populateProtonVersions();
    if (!exe.protonVersion.isEmpty())
        m_protonCombo->setCurrentText(exe.protonVersion);
    m_prefixEdit = new QLineEdit(
        exe.winePrefix.isEmpty()
            ? QDir::homePath() + "/.local/share/solero/tools-prefix"
            : exe.winePrefix,
        m_protonGroup);
    protonLayout->addRow("Proton version:", m_protonCombo);
    protonLayout->addRow("Wine prefix:", m_prefixEdit);
    m_protonGroup->setVisible(exe.runtime == RuntimeType::Proton);

    m_workdirEdit = new QLineEdit(exe.workingDir, this);

    m_outputCombo = new QComboBox(this);
    setOutputModChoices({}, exe.outputModId);

    layout->addRow("Name:", m_nameEdit);
    layout->addRow("Binary:", binRow);
    layout->addRow("Arguments:", m_argsEdit);
    layout->addRow("Working dir:", m_workdirEdit);
    layout->addRow("Output mod:", m_outputCombo);
    layout->addRow("Runtime:", m_runtimeCombo);
    layout->addRow(m_protonGroup);
    layout->addRow(m_deployCheck);
    layout->addRow(m_primaryCheck);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addRow(btns);

    connect(browseBtn, &QPushButton::clicked, this, &ExecutableDialog::browseForBinary);
    connect(m_runtimeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExecutableDialog::onRuntimeChanged);
    connect(btns, &QDialogButtonBox::accepted, this, [this]{
        m_result.name              = m_nameEdit->text();
        m_result.binaryPath        = m_binaryEdit->text();
        m_result.arguments         = m_argsEdit->text();
        m_result.workingDir        = m_workdirEdit->text();
        m_result.runtime           = m_runtimeCombo->currentIndex() == 1
                                     ? RuntimeType::Proton : RuntimeType::Native;
        m_result.protonVersion     = m_protonCombo->currentText();
        m_result.winePrefix        = m_prefixEdit->text();
        m_result.runThroughDeployer= m_deployCheck->isChecked();
        m_result.isPrimary         = m_primaryCheck->isChecked();
        m_result.outputModId       = m_outputCombo->currentData().toString();
        m_result.isCapturingOutput = !m_result.outputModId.isEmpty();
        accept();
    });
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ExecutableDialog::setOutputModChoices(const QList<QPair<QString,QString>>& idName, const QString& currentId) {
    m_outputCombo->clear();
    m_outputCombo->addItem("(None - Overwrite)", QVariant(QString()));
    for (const auto& [id, name] : idName)
        m_outputCombo->addItem(name, id);
    int idx = m_outputCombo->findData(currentId);
    m_outputCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void ExecutableDialog::browseForBinary() {
    QString path = QFileDialog::getOpenFileName(this, "Select Binary", QDir::homePath());
    if (!path.isEmpty()) m_binaryEdit->setText(path);
}

void ExecutableDialog::onRuntimeChanged(int idx) {
    m_protonGroup->setVisible(idx == 1);
}

void ExecutableDialog::populateProtonVersions() {
    QStringList paths = {
        QDir::homePath() + "/.steam/root/compatibilitytools.d",
        QDir::homePath() + "/.local/share/Steam/steamapps/common"
    };
    m_protonCombo->addItem("(select version)");
    for (const auto& base : paths) {
        QDir d(base);
        if (!d.exists()) continue;
        for (const auto& entry : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (entry.contains("Proton", Qt::CaseInsensitive))
                m_protonCombo->addItem(entry);
        }
    }
}

} // namespace solero
