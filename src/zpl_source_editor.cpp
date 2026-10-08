#include "zpl_source_editor.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QVBoxLayout>

#include <algorithm>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
class ZplHighlighter final : public QSyntaxHighlighter {
public:
    explicit ZplHighlighter(QPlainTextEdit* editor)
        : QSyntaxHighlighter(editor->document()), editor_(editor) {}
protected:
    void highlightBlock(const QString& text) override
    {
        QTextCharFormat command, data, comment;
        command.setForeground(editor_->palette().brush(QPalette::Link));
        command.setFontWeight(QFont::Bold);
        data.setForeground(editor_->palette().brush(QPalette::Text));
        comment.setForeground(editor_->palette().brush(QPalette::PlaceholderText));
        comment.setFontItalic(true);
        const QRegularExpression commands(u"[\\^~][A-Za-z0-9][A-Za-z0-9@]"_s);
        int state = std::max(0, previousBlockState());
        int position = 0;
        while (position < text.size()) {
            // Field data can contain command-like text; only ^FS ends it.
            if (state == 1) {
                const int end = static_cast<int>(text.indexOf(u"^FS"_s, position, Qt::CaseInsensitive));
                if (end < 0) { setFormat(position, text.size() - position, data); break; }
                setFormat(position, end - position, data);
                position = end;
                state = 0;
            }
            const auto match = commands.match(text, position);
            const int next = match.hasMatch() ? static_cast<int>(match.capturedStart()) : static_cast<int>(text.size());
            if (state == 2) setFormat(position, next - position, comment);
            if (!match.hasMatch()) break;
            const auto opcode = match.captured().mid(1).toUpper();
            setFormat(next, match.capturedLength(), command);
            state = opcode == u"FD" || opcode == u"FV" ? 1 : opcode == u"FX" ? 2 : 0;
            position = next + static_cast<int>(match.capturedLength());
        }
        setCurrentBlockState(state);
    }
private:
    QPlainTextEdit* editor_;
};
} // namespace

class SourceLineNumbers final : public QWidget {
public:
    explicit SourceLineNumbers(ZplSourceEditor* editor) : QWidget(editor), editor_(editor)
    {
        setObjectName(u"sourceLineNumbers"_s);
        setAccessibleName(u"Номера строк ZPL"_s);
    }
    QSize sizeHint() const override { return {editor_->lineNumberWidth(), 0}; }
protected:
    void paintEvent(QPaintEvent* event) override { editor_->paintLineNumbers(event); }
private:
    ZplSourceEditor* editor_;
};

ZplSourceEditor::ZplSourceEditor(QWidget* parent) : QPlainTextEdit(parent)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    lineNumbers_ = new SourceLineNumbers(this);
    highlighter_ = new ZplHighlighter(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, &ZplSourceEditor::updateLineNumberWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
        if (dy) lineNumbers_->scroll(0, dy);
        else lineNumbers_->update(0, rect.y(), lineNumbers_->width(), rect.height());
        if (rect.contains(viewport()->rect())) updateLineNumberWidth();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        highlightCurrentLine();
        lineNumbers_->update();
    });
    const auto shortcut = [this](const QKeySequence& key, auto callback) {
        auto* action = new QShortcut(key, this);
        action->setContext(Qt::WidgetWithChildrenShortcut);
        connect(action, &QShortcut::activated, this, callback);
    };
    shortcut(QKeySequence::Find, &ZplSourceEditor::showFind);
    shortcut(QKeySequence(u"Ctrl+H"_s), &ZplSourceEditor::showReplace);
    shortcut(QKeySequence::FindNext, &ZplSourceEditor::findNext);
    shortcut(QKeySequence::FindPrevious, &ZplSourceEditor::findPrevious);
    updateLineNumberWidth();
    highlightCurrentLine();
}

int ZplSourceEditor::lineNumberWidth() const
{
    return 14 + fontMetrics().horizontalAdvance(u'9') * QString::number(blockCount()).size();
}

void ZplSourceEditor::updateLineNumberWidth()
{
    setViewportMargins(lineNumberWidth(), 0, 0, 0);
    const QRect rect = contentsRect();
    lineNumbers_->setGeometry(rect.left(), rect.top(), lineNumberWidth(), rect.height());
}

void ZplSourceEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    updateLineNumberWidth();
}

void ZplSourceEditor::changeEvent(QEvent* event)
{
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange) {
        // Rehighlighting changes layout formats only. Do not report this as
        // a user edit to the session or invalidate an existing label preview.
        if (highlighter_) {
            const QSignalBlocker blocker(this);
            highlighter_->rehighlight();
        }
        if (lineNumbers_) {
            if (event->type() == QEvent::FontChange) updateLineNumberWidth();
            lineNumbers_->update();
        }
        highlightCurrentLine();
    }
}

void ZplSourceEditor::paintLineNumbers(QPaintEvent* event)
{
    QPainter painter(lineNumbers_);
    painter.fillRect(event->rect(), palette().brush(QPalette::AlternateBase));
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    const int current = textCursor().blockNumber();
    while (block.isValid() && top <= event->rect().bottom()) {
        const int height = qRound(blockBoundingRect(block).height());
        if (block.isVisible() && top + height >= event->rect().top()) {
            painter.setPen(palette().color(number == current ? QPalette::Text : QPalette::PlaceholderText));
            painter.drawText(0, top, lineNumbers_->width() - 6, fontMetrics().height(),
                             Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next();
        top += height;
        ++number;
    }
}

void ZplSourceEditor::highlightCurrentLine()
{
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(palette().brush(QPalette::AlternateBase));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    setExtraSelections({selection});
}

void ZplSourceEditor::goToSourceOffset(int offset)
{
    QTextCursor cursor(document());
    cursor.setPosition(std::clamp(offset, 0, std::max(0, document()->characterCount() - 1)));
    setTextCursor(cursor);
    ensureCursorVisible();
    setFocus(Qt::OtherFocusReason);
}

void ZplSourceEditor::setSearchText(const QString& text, bool caseSensitive)
{
    searchText_ = text;
    caseSensitive_ = caseSensitive;
    if (search_) search_->setText(text);
    if (matchCase_) matchCase_->setChecked(caseSensitive);
}

void ZplSourceEditor::setReplacementText(const QString& text)
{
    replacementText_ = text;
    if (replacement_) replacement_->setText(text);
}

QTextDocument::FindFlags ZplSourceEditor::searchFlags() const
{
    return caseSensitive_ ? QTextDocument::FindCaseSensitively : QTextDocument::FindFlags{};
}

void ZplSourceEditor::setSearchStatus(const QString& text)
{
    if (searchStatus_) searchStatus_->setText(text);
}

bool ZplSourceEditor::findMatch(bool backwards)
{
    if (searchText_.isEmpty()) { setSearchStatus(u"Введите текст для поиска."_s); return false; }
    auto flags = searchFlags();
    if (backwards) flags |= QTextDocument::FindBackward;
    auto found = document()->find(searchText_, textCursor(), flags);
    bool wrapped = false;
    if (found.isNull()) {
        const int start = backwards ? document()->characterCount() - 1 : 0;
        found = document()->find(searchText_, start, flags);
        wrapped = true;
    }
    if (found.isNull()) { setSearchStatus(u"Совпадений нет."_s); return false; }
    setTextCursor(found);
    ensureCursorVisible();
    setSearchStatus(wrapped ? u"Поиск продолжен с начала/конца документа."_s : QString{});
    return true;
}

bool ZplSourceEditor::findNext() { return findMatch(false); }
bool ZplSourceEditor::findPrevious() { return findMatch(true); }

bool ZplSourceEditor::replaceCurrent()
{
    if (isReadOnly() || searchText_.isEmpty()) return false;
    auto cursor = textCursor();
    const auto sensitivity = caseSensitive_ ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (!cursor.hasSelection() || cursor.selectedText().compare(searchText_, sensitivity) != 0) {
        if (!findNext()) return false;
        cursor = textCursor();
    }
    cursor.insertText(replacementText_);
    setTextCursor(cursor);
    findNext();
    return true;
}

int ZplSourceEditor::replaceAll()
{
    if (isReadOnly() || searchText_.isEmpty()) return 0;
    QTextCursor edit(document());
    edit.beginEditBlock();
    auto match = document()->find(searchText_, 0, searchFlags());
    int count = 0;
    while (!match.isNull()) {
        match.insertText(replacementText_);
        ++count;
        // Continue after the inserted replacement so it is never searched again.
        match = document()->find(searchText_, match, searchFlags());
    }
    edit.endEditBlock();
    setSearchStatus(u"Заменено: %1."_s.arg(count));
    return count;
}

void ZplSourceEditor::createSearchDialog()
{
    searchDialog_ = new QDialog(this);
    searchDialog_->setObjectName(u"sourceFindDialog"_s);
    searchDialog_->setModal(false);
    auto* layout = new QVBoxLayout(searchDialog_);
    auto* row = new QHBoxLayout;
    auto* label = new QLabel(u"Найти:"_s, searchDialog_);
    search_ = new QLineEdit(searchText_, searchDialog_);
    search_->setObjectName(u"sourceFindText"_s);
    label->setBuddy(search_);
    row->addWidget(label); row->addWidget(search_, 1);
    const auto addButton = [this](QHBoxLayout* target, const QString& text, const QString& name, auto callback) {
        auto* button = new QPushButton(text, searchDialog_);
        button->setObjectName(name);
        button->setAutoDefault(false);
        target->addWidget(button);
        connect(button, &QPushButton::clicked, this, callback);
    };
    addButton(row, u"Назад"_s, u"sourceFindPrevious"_s, &ZplSourceEditor::findPrevious);
    addButton(row, u"Далее"_s, u"sourceFindNext"_s, &ZplSourceEditor::findNext);
    layout->addLayout(row);
    matchCase_ = new QCheckBox(u"Учитывать регистр"_s, searchDialog_);
    matchCase_->setChecked(caseSensitive_);
    layout->addWidget(matchCase_);
    replacementRow_ = new QWidget(searchDialog_);
    auto* replaceRow = new QHBoxLayout(replacementRow_);
    replaceRow->setContentsMargins(0, 0, 0, 0);
    auto* replacementLabel = new QLabel(u"Заменить на:"_s, replacementRow_);
    replacement_ = new QLineEdit(replacementText_, replacementRow_);
    replacement_->setObjectName(u"sourceReplaceText"_s);
    replacementLabel->setBuddy(replacement_);
    replaceRow->addWidget(replacementLabel); replaceRow->addWidget(replacement_, 1);
    addButton(replaceRow, u"Заменить"_s, u"sourceReplaceCurrent"_s, &ZplSourceEditor::replaceCurrent);
    addButton(replaceRow, u"Все"_s, u"sourceReplaceAll"_s, &ZplSourceEditor::replaceAll);
    layout->addWidget(replacementRow_);
    searchStatus_ = new QLabel(searchDialog_);
    searchStatus_->setWordWrap(true);
    layout->addWidget(searchStatus_);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString& value) { searchText_ = value; setSearchStatus({}); });
    connect(replacement_, &QLineEdit::textChanged, this, [this](const QString& value) { replacementText_ = value; });
    connect(matchCase_, &QCheckBox::toggled, this, [this](bool value) { caseSensitive_ = value; });
    connect(search_, &QLineEdit::returnPressed, this, &ZplSourceEditor::findNext);
    connect(replacement_, &QLineEdit::returnPressed, this, &ZplSourceEditor::replaceCurrent);
}

void ZplSourceEditor::showSearch(bool replace)
{
    const auto selected = textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator)) searchText_ = selected;
    if (!searchDialog_) createSearchDialog();
    search_->setText(searchText_);
    searchDialog_->setWindowTitle(replace ? u"Поиск и замена ZPL"_s : u"Поиск в ZPL"_s);
    replacementRow_->setVisible(replace);
    replacementRow_->setEnabled(!isReadOnly());
    searchDialog_->show();
    searchDialog_->raise();
    searchDialog_->activateWindow();
    search_->setFocus();
    search_->selectAll();
}

void ZplSourceEditor::showFind() { showSearch(false); }
void ZplSourceEditor::showReplace() { showSearch(true); }
} // namespace Designer
