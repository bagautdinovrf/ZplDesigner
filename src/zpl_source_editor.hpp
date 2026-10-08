#pragma once

#include <QPlainTextEdit>
#include <QTextDocument>

class QCheckBox;
class QDialog;
class QLabel;
class QLineEdit;
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
    [[nodiscard]] int lineNumberWidth() const;

public slots:
    void showFind();
    void showReplace();
    void goToSourceOffset(int offset);
    bool findNext();
    bool findPrevious();
    bool replaceCurrent();
    int replaceAll();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    friend class SourceLineNumbers;
    void paintLineNumbers(QPaintEvent* event);
    void updateLineNumberWidth();
    void highlightCurrentLine();
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
    bool caseSensitive_ = false;
};
} // namespace Designer
