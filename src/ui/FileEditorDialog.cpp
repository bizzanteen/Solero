#include "FileEditorDialog.h"
#include "core/FileUtil.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QMessageBox>
#include <QStringConverter>
#include <sys/stat.h>

namespace {
// Refuse to load files larger than this into the editor.
constexpr qint64 kMaxEditBytes = 8 * 1024 * 1024; // ~8 MB
}

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
    const qint64 fileSize = QFileInfo(filePath).size();
    if (fileSize > kMaxEditBytes) {
        // Too large to load into a text editor; refuse and disable editing.
        m_binary = true; // reuse the read-only path (no Save button)
        const double mb = static_cast<double>(fileSize) / (1024.0 * 1024.0);
        m_edit->setPlainText(
            QString("File too large to edit in Solero (%1 MB).").arg(mb, 0, 'f', 1));
        m_edit->setReadOnly(true);
        m_statusLabel->setText(filePath + "  -  too large, not loaded");
        m_statusLabel->setStyleSheet("color: #c0392b;");
    } else if (!f.open(QIODevice::ReadOnly)) {
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
            // Try UTF-8 with error detection; fall back to Latin-1. Remember the
            // chosen encoding so we re-encode the same way on save.
            QStringDecoder utf8(QStringConverter::Utf8);
            QString text = utf8.decode(data);
            if (utf8.hasError()) {
                QStringDecoder latin1(QStringConverter::Latin1);
                text = latin1.decode(data);
                m_encoding = QStringConverter::Latin1;
            } else {
                m_encoding = QStringConverter::Utf8;
            }
            m_edit->setPlainText(text);
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

// True if the path is a symlink or a hardlinked regular file (st_nlink > 1).
// Such files must be written in place: under hardlink/symlink deploy the staged
// file and the live game file are the same inode, so an in-place write updates
// both at once and survives undeploy/redeploy. atomicWrite()'s temp+rename would
// replace the path with a fresh inode, breaking the link so the edit lands on
// only one side and a redeploy reverts it.
static bool isLinkedFile(const QString& path) {
    if (QFileInfo(path).isSymLink()) return true;
    struct stat st;
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 && st.st_nlink > 1;
}

// In-place write: truncate the existing inode and rewrite it, preserving every
// hardlink to it (and writing through a symlink to its target).
static bool writeInPlace(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const bool ok = f.write(data) == data.size();
    f.close();
    return ok;
}

void FileEditorDialog::save() {
    if (m_binary) return;
    // Re-encode using the encoding the file was loaded with, so Latin-1 INIs
    // round-trip without corruption.
    QStringEncoder enc(m_encoding);
    const QByteArray data = enc.encode(m_edit->toPlainText());
    // Preserve hardlinks/symlinks (deployed files) so the edit reaches the game
    // immediately; only fall back to the crash-safe atomic write for a standalone
    // file that isn't part of a deployment.
    const bool ok = isLinkedFile(m_filePath) ? writeInPlace(m_filePath, data)
                                             : atomicWrite(m_filePath, data);
    if (!ok) {
        QMessageBox::warning(this, "Save Failed",
            "Could not write to:\n" + m_filePath);
        m_statusLabel->setText(m_filePath + "  -  save failed");
        m_statusLabel->setStyleSheet("color: #c0392b;");
        return;
    }
    m_statusLabel->setText(m_filePath + "  -  saved");
    m_statusLabel->setStyleSheet("color: #27ae60;");
    emit fileSaved(m_filePath);
}

} // namespace solero
