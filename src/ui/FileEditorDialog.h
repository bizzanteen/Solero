#pragma once
#include <QDialog>

class QPlainTextEdit;
class QLabel;

namespace solero {

// A lightweight non-modal text editor window for a single staged mod file.
// Binary files are opened read-only. Emits fileSaved() after a successful write.
class FileEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit FileEditorDialog(const QString& filePath, QWidget* parent = nullptr);

signals:
    void fileSaved(const QString& filePath);

private:
    void save();
    static bool looksBinary(const QByteArray& data);

    QString         m_filePath;
    QPlainTextEdit* m_edit;
    QLabel*         m_statusLabel;
    bool            m_binary = false;
};

} // namespace solero
