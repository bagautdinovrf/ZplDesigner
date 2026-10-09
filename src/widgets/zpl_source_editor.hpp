#pragma once

#include <QPlainTextEdit>
#include <QPair>
#include <QTextDocument>

class QCheckBox;
class QDialog;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QSyntaxHighlighter;

namespace Designer {
class SourceLineNumbers;

// Source editing never normalizes or rewrites unsupported commands.
class ZplSourceEditor final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit ZplSourceEditor(QWidget* parent = nullptr);
    void setSearchText(const QString& text, bool caseSensitive = false);
    void setReplacementText(const QString& text);
    // UTF-16 offset/length pairs; paint matches without moving the edit cursor.
    void setObjectHighlights(const QList<QPair<int, int>>& ranges, bool reveal = false);
    [[nodiscard]] int lineNumberWidth() const;

public slots:
    void showFind();
    void showReplace();
    void goToSourceOffset(int offset);
    bool findNext();
    bool findPrevious();
    bool replaceCurrent();
    int replaceAll();

signals:
    // Explicit clicks also activate a field when the caret has not moved.
    void sourceCursorActivated();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    friend class SourceLineNumbers;
    void paintLineNumbers(QPaintEvent* event);
    void updateLineNumberWidth();
    void highlightCurrentLine();
    void revealObjectHighlight();
    void showSearch(bool replace);
    void createSearchDialog();
    bool findMatch(bool backwards);
    QTextDocument::FindFlags searchFlags() const;
    void setSearchStatus(const QString& text);

    SourceLineNumbers* lineNumbers_ = nullptr;
    QSyntaxHighlighter* highlighter_ = nullptr;
    QDialog* searchDialog_ = nullptr;
    QLineEdit* search_ = nullptr;
    QLineEdit* replacement_ = nullptr;
    QCheckBox* matchCase_ = nullptr;
    QWidget* replacementRow_ = nullptr;
    QLabel* searchStatus_ = nullptr;
    QString searchText_;
    QString replacementText_;
    QString highlightedSource_;
    QList<QPair<int, int>> objectHighlights_;
    bool caseSensitive_ = false;
};
} // namespace Designer
