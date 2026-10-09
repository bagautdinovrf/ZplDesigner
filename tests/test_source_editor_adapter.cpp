#include "document_session.hpp"
#include "source_editor_adapter.hpp"
#include "zpl_source_editor.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
QString editableSource()
{
    return u"^XA\r\n^CI28^PW400^LL200\r\n^FO20,30^A0N,20,10^FDПервый \U0001F642^FS"
           u"^FO120,30^A0N,20,10^FDВторой^FS\r\n^XZ\r\n"_s;
}

DesignDocument editableDocument()
{
    return *fromZpl(editableSource());
}
} // namespace

class SourceEditorAdapterTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void applyPreservesCursorAndNativeUndo();
    void selectionWaitsForExactAppliedSource();
    void designEditDoesNotRetargetOldCaret();
    void resetInvalidatesScheduledApply();
    void newerDraftInvalidatesScheduledApply();
    void rejectedDraftKeepsEditorAndLastAppliedDocument();
};

void SourceEditorAdapterTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(u"QtZplTests"_s);
    QCoreApplication::setApplicationName(u"source-adapter-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

void SourceEditorAdapterTests::cleanupTestCase()
{
    const QDir base(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    for (const auto& name : {u"recovery"_s, u"layouts"_s}) {
        const QDir directory(base.filePath(name));
        for (const auto& file : directory.entryInfoList(QDir::Files)) QVERIFY(QFile::remove(file.absoluteFilePath()));
        if (directory.exists()) QVERIFY(QDir().rmdir(directory.absolutePath()));
    }
    if (base.exists()) QVERIFY(QDir().rmdir(base.absolutePath()));
    QStandardPaths::setTestModeEnabled(false);
}

void SourceEditorAdapterTests::applyPreservesCursorAndNativeUndo()
{
    DocumentSession session;
    session.reset(editableDocument());
    ZplSourceEditor editor;
    SourceEditorAdapter adapter(session, editor);
    connect(&session, &DocumentSession::documentChanged, &adapter, &SourceEditorAdapter::syncDocument);
    adapter.syncDocument();
    const auto original = editor.toPlainText();
    QVERIFY(!original.contains(u'\r'));
    auto cursor = editor.textCursor();
    const auto offset = static_cast<int>(original.indexOf(u"20,30"));
    QVERIFY(offset >= 0);
    cursor.setPosition(offset);
    cursor.setPosition(offset + 2, QTextCursor::KeepAnchor);
    cursor.insertText(u"60"_s);
    editor.setTextCursor(cursor);
    const auto entered = editor.toPlainText();
    const int position = cursor.position();
    QSignalSpy edits(&adapter, &SourceEditorAdapter::sourceEdited);
    QSignalSpy changes(&editor, &QPlainTextEdit::textChanged);
    const auto applied = adapter.apply();
    QVERIFY(applied.has_value());
    QVERIFY(*applied);
    QCOMPARE(session.document().elements[0].x, 60);
    QCOMPARE(editor.toPlainText(), entered);
    QCOMPARE(editor.textCursor().position(), position);
    QCOMPARE(edits.size(), 0);
    QCOMPARE(changes.size(), 0);
    QVERIFY(editor.document()->isUndoAvailable());
    editor.undo();
    QCOMPARE(editor.toPlainText(), original);
    QVERIFY(session.hasPendingSource());
    QVERIFY(adapter.apply().has_value());
    QCOMPARE(session.document().elements[0].x, 20);
    QVERIFY(editor.document()->isRedoAvailable());
    editor.redo();
    QCOMPARE(editor.toPlainText(), entered);
    session.discardRecovery();
}

void SourceEditorAdapterTests::selectionWaitsForExactAppliedSource()
{
    DocumentSession session;
    session.reset(editableDocument());
    ZplSourceEditor editor;
    SourceEditorAdapter adapter(session, editor);
    connect(&session, &DocumentSession::documentChanged, &adapter, [&] {
        adapter.syncDocument();
        adapter.resumeSelection();
    });
    adapter.syncDocument();
    QSignalSpy selections(&adapter, &SourceEditorAdapter::selectedIndicesRequested);
    auto cursor = editor.textCursor();
    const auto offset = static_cast<int>(editor.toPlainText().indexOf(u"Второй"));
    cursor.setPosition(offset);
    editor.setTextCursor(cursor);
    adapter.sourceSelection(true, true);
    QVERIFY(!selections.isEmpty());
    QCOMPARE(qvariant_cast<QList<int>>(selections.last().first()), QList<int>{1});

    cursor.insertText(u"Новый "_s);
    editor.setTextCursor(cursor);
    QVERIFY(session.hasPendingSource());
    QVERIFY(!adapter.sourceMatchesDocument());
    selections.clear();
    adapter.sourceSelection(true, true);
    QCOMPARE(selections.size(), 0);
    QVERIFY(adapter.apply().has_value());
    QVERIFY(adapter.sourceMatchesDocument());
    QVERIFY(!selections.isEmpty());
    QCOMPARE(qvariant_cast<QList<int>>(selections.last().first()), QList<int>{1});
    session.discardRecovery();
}

void SourceEditorAdapterTests::designEditDoesNotRetargetOldCaret()
{
    DocumentSession session;
    session.reset(editableDocument());
    ZplSourceEditor editor;
    SourceEditorAdapter adapter(session, editor);
    connect(&session, &DocumentSession::documentChanged, &adapter, [&] {
        adapter.syncDocument();
        adapter.resumeSelection();
    });
    adapter.syncDocument();
    auto cursor = editor.textCursor();
    cursor.setPosition(static_cast<int>(editor.toPlainText().indexOf(u"Второй")));
    editor.setTextCursor(cursor);
    adapter.sourceSelection(true, true);
    QSignalSpy selections(&adapter, &SourceEditorAdapter::selectedIndicesRequested);
    auto changed = session.document();
    changed.elements[0].text = u"X"_s;
    session.commit(changed, u"Shorten first field"_s);
    QCOMPARE(selections.size(), 0);
    QVERIFY(adapter.sourceMatchesDocument());
    QVERIFY(!session.hasPendingSource());
    session.discardRecovery();
}

void SourceEditorAdapterTests::resetInvalidatesScheduledApply()
{
    DocumentSession session;
    session.reset(editableDocument());
    ZplSourceEditor editor;
    SourceEditorAdapter adapter(session, editor);
    adapter.syncDocument();
    session.setPendingSource(u"^XA^PW320^LL160^FO20,30^A0N,24,12^FDChanged^FS^XZ"_s);
    adapter.scheduleApply();
    const auto previousInstance = session.revision().instance;
    session.reset(editableDocument());
    QVERIFY(session.revision().instance != previousInstance);
    session.setPendingSource(u"^XA^PW640^LL160^FO20,30^A0N,24,12^FDNew document draft^FS^XZ"_s);
    QSignalSpy finished(&adapter, &SourceEditorAdapter::applyFinished);
    QTest::qWait(380);
    QVERIFY(session.hasPendingSource());
    QCOMPARE(session.document().width, 400);
    QCOMPARE(session.undoStack()->count(), 0);
    QCOMPARE(finished.size(), 0);
    adapter.scheduleApply();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 1500);
    QVERIFY(finished.first().first().toBool());
    QCOMPARE(session.document().width, 640);
    session.discardRecovery();
}

void SourceEditorAdapterTests::newerDraftInvalidatesScheduledApply()
{
    DocumentSession session;
    session.reset(editableDocument());
    ZplSourceEditor editor;
    SourceEditorAdapter adapter(session, editor);
    adapter.syncDocument();
    session.setPendingSource(u"^XA^PW320^LL160^FO20,30^A0N,24,12^FDFirst^FS^XZ"_s);
    adapter.scheduleApply();
    const auto newer = u"^XA^PW640^LL160^FO20,30^A0N,24,12^FDSecond^FS^XZ"_s;
    session.setPendingSource(newer);
    QSignalSpy finished(&adapter, &SourceEditorAdapter::applyFinished);
    QTest::qWait(380);
    QCOMPARE(session.pendingSource(), newer);
    QCOMPARE(session.document().width, 400);
    QCOMPARE(session.undoStack()->count(), 0);
    QCOMPARE(finished.size(), 0);
    session.discardRecovery();
}

void SourceEditorAdapterTests::rejectedDraftKeepsEditorAndLastAppliedDocument()
{
    DocumentSession session;
    session.reset(editableDocument());
    const auto applied = toZpl(session.document());
    ZplSourceEditor editor;
    SourceEditorAdapter adapter(session, editor);
    adapter.syncDocument();
    const auto invalid = u"^XA\n^FO123,"_s;
    editor.setPlainText(invalid);
    QVERIFY(session.hasPendingSource());
    QVERIFY(!adapter.apply().has_value());
    QCOMPARE(session.sourceStatus(), SourceStatus::Rejected);
    adapter.syncDocument();
    QCOMPARE(editor.toPlainText(), invalid);
    QVERIFY(!editor.isReadOnly());
    QVERIFY(!adapter.sourceMatchesDocument());
    QCOMPARE(toZpl(session.document()), applied);
    session.discardRecovery();
}

QTEST_MAIN(SourceEditorAdapterTests)
#include "test_source_editor_adapter.moc"
