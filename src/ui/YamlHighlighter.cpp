#include "YamlHighlighter.h"

YamlHighlighter::YamlHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
    QTextCharFormat keyFormat;
    keyFormat.setForeground(QColor("#569cd6"));
    m_rules.append({QRegularExpression(R"(^\s*[\w\-]+:)"), keyFormat});

    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor("#ce9178"));
    m_rules.append({QRegularExpression(R"("[^"]*")"), stringFormat});

    QTextCharFormat commentFormat;
    commentFormat.setForeground(QColor("#6a9955"));
    commentFormat.setFontItalic(true);
    m_rules.append({QRegularExpression(R"(#[^\n]*)"), commentFormat});

    QTextCharFormat listFormat;
    listFormat.setForeground(QColor("#4ec9b0"));
    m_rules.append({QRegularExpression(R"(^\s*-\s)"), listFormat});
}

void YamlHighlighter::highlightBlock(const QString& text) {
    for (const auto& rule : m_rules) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
}
