#include "FileEditorDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QMessageBox>

namespace solero {

FileEditorDialog::FileEditorDialog(const QString& filePath, QWidget* parent)
    : QDialog(parent), m_filePath(filePath) {
    setWindowTitle(QFileInfo(filePath).fileName() + " - Solero Editor");
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(720, 560);

    auto* layout = new QVBoxLayout(this);

    m_statusLabel = new QLabel(filePath, this);
    m_statusLabel->setStyleSheet("color: gray;");
    layout->addWidget(m_statusLabel);

    m_edit = new QPlainTextEdit(this);
    QFont mono("Monospace");
    mono.setStyleHint(QFont::TypeWriter);
    mono.setPointSize(10);
    m_edit->setFont(mono);
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(m_edit);

    // Load the file
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        m_edit->setPlainText("(could not open file)");
        m_edit->setReadOnly(true);
        m_binary = true;
    } else {
        QByteArray data = f.readAll();
        m_binary = looksBinary(data);
        if (m_binary) {
            m_edit->setPlainText("(binary file - read only)\n\nThis file appears to be binary "
                                 "(e.g. a .dll or compiled script) and cannot be safely edited as text.");
            m_edit->setReadOnly(true);
            m_statusLabel->setText(filePath + "  -  binary, read-only");
            m_statusLabel->setStyleSheet("color: #c0392b;");
        } else {
            m_edit->setPlainText(QString::fromUtf8(data));
        }
    }

    // Buttons
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    if (!m_binary) {
        auto* saveBtn = new QPushButton("Save", this);
        saveBtn->setDefault(true);
        connect(saveBtn, &QPushButton::clicked, this, &FileEditorDialog::save);
        btnRow->addWidget(saveBtn);
    }
    auto* closeBtn = new QPushButton("Close", this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}

bool FileEditorDialog::looksBinary(const QByteArray& data) {
    // Treat a NUL byte in the first 8 KB as a binary signal.
    const int limit = qMin(data.size(), 8192);
    for (int i = 0; i < limit; ++i)
        if (data.at(i) == '\0') return true;
    return false;
}

void FileEditorDialog::save() {
    if (m_binary) return;
    QFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Save Failed",
            "Could not write to:\n" + m_filePath);
        return;
    }
    f.write(m_edit->toPlainText().toUtf8());
    f.close();
    m_statusLabel->setText(m_filePath + "  -  saved");
    m_statusLabel->setStyleSheet("color: #27ae60;");
    emit fileSaved(m_filePath);
}

} // namespace solero
