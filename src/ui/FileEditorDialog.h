#pragma once
#include <QDialog>
#include <QStringConverter>

class QPlainTextEdit;
class QLabel;
class QCloseEvent;

namespace solero {

// A lightweight non-modal text editor window for a single staged mod file.
// Binary files are opened read-only. Emits fileSaved() after a successful write.
class FileEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit FileEditorDialog(const QString& filePath, QWidget* parent = nullptr);

signals:
    void fileSaved(const QString& filePath);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void save();
    static bool looksBinary(const QByteArray& data);

    QString         m_filePath;
    QPlainTextEdit* m_edit;
    QLabel*         m_statusLabel;
    bool            m_binary = false;
    // Encoding the file was decoded with; re-used on save so e.g. Latin-1 INIs
    // round-trip without corruption.
    QStringConverter::Encoding m_encoding = QStringConverter::Utf8;
};

} // namespace solero
