#include "document_session.hpp"
#include "project_codec.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
DesignDocument editableDocument()
{
    DesignDocument document;
    document.dpi = 300;
    auto text = makeElement(ElementType::Text, {24, 32});
    text.text = u"Part {{part}}"_s;
    document.elements = {text};
    return document;
}

QString replacementSource()
{
    return u"^XA\n^PW320^LL160^FO20,30^A0N,24,12^FDReplacement^FS^XZ\n"_s;
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
} // namespace

class SourceStateTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void successfulApplyPublishesOneCoherentState();
    void documentObserversSeeCompletedHistoryAndSavedState();
    void rejectedDraftKeepsAppliedDocumentAndHistory();
    void sourceOnlyApplyIsLossless_data();
    void sourceOnlyApplyIsLossless();
    void staleApplyCannotCrossDocumentOrDraftRevisions();
    void pendingSourceBlocksVisualMutationsAndExport();
    void rejectedDraftCanBeSavedWithoutEnablingExport();
    void openingProjectPublishesDraftWithDocument();
    void recoveryPublishesDraftWithDocument();
    void resetClearsDraftBeforeNotifyingObservers();
    void unchangedSourceAddsNoHistory();
    void previewValuesSurviveApplyUndoAndRedo();
};

void SourceStateTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(u"QtZplTests"_s);
    QCoreApplication::setApplicationName(u"source-state-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

void SourceStateTests::cleanupTestCase()
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

void SourceStateTests::successfulApplyPublishesOneCoherentState()
{
    DocumentSession session;
    session.reset(editableDocument());
    const auto before = session.revision();
    session.setPendingSource(replacementSource());
    QCOMPARE(session.sourceStatus(), SourceStatus::Pending);
    QVERIFY(!session.canEditVisually());
    bool coherent = true;
    const auto observe = [&] {
        coherent = coherent && !session.hasPendingSource()
            && session.sourceStatus() == SourceStatus::Applied && session.sourceError().isEmpty()
            && session.document().width == 320 && session.document().dpi == 300
            && session.revision().instance == before.instance
            && session.revision().document > before.document;
    };
    connect(&session, &DocumentSession::documentChanged, this, observe);
    connect(&session, &DocumentSession::pendingSourceChanged, this, observe);
    QSignalSpy changed(&session, &DocumentSession::documentChanged);
    const auto applied = session.applySource();
    QVERIFY2(applied.has_value(), qPrintable(applied ? QString{} : applied.error()));
    QVERIFY(*applied);
    QVERIFY(coherent);
    QCOMPARE(changed.size(), 1);
    QCOMPARE(session.undoStack()->count(), 1);
    QCOMPARE(toZpl(session.document()), replacementSource());
    QVERIFY(session.canEditVisually());
    session.discardRecovery();
}

void SourceStateTests::documentObserversSeeCompletedHistoryAndSavedState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DocumentSession session;
    const auto original = editableDocument();
    session.reset(original);
    const auto path = directory.filePath(u"history.qtzpl"_s);
    QVERIFY(session.save(path).has_value());
    const auto originalSource = toZpl(session.document());
    int expectedIndex = 1;
    int expectedCount = 1;
    bool expectedModified = true;
    QString expectedSource = replacementSource();
    bool coherent = true;
    connect(&session, &DocumentSession::documentChanged, this, [&] {
        coherent = coherent && session.undoStack()->index() == expectedIndex
            && session.undoStack()->count() == expectedCount && session.modified() == expectedModified
            && session.sourceStatus() == SourceStatus::Applied && !session.hasPendingSource()
            && toZpl(session.document()) == expectedSource;
    });
    QSignalSpy changed(&session, &DocumentSession::documentChanged);
    session.setPendingSource(replacementSource());
    const auto applied = session.applySource();
    QVERIFY(applied.has_value());
    QVERIFY(*applied);
    QCOMPARE(changed.size(), 1);
    QVERIFY(coherent);

    // Saving makes history index 1 clean; undoing to the old document is now
    // dirty even though index 0 was clean before the save.
    QVERIFY(session.save(path).has_value());
    QVERIFY(!session.modified());
    QCOMPARE(changed.size(), 1);
    expectedIndex = 0;
    expectedSource = originalSource;
    session.undoStack()->undo();
    QCOMPARE(changed.size(), 2);
    QVERIFY(coherent);
    expectedIndex = 1;
    expectedModified = false;
    expectedSource = replacementSource();
    session.undoStack()->redo();
    QCOMPARE(changed.size(), 3);
    QVERIFY(coherent);

    expectedIndex = 0;
    expectedCount = 0;
    expectedSource = originalSource;
    session.reset(original);
    QCOMPARE(changed.size(), 4);
    QVERIFY(coherent);
    // A reset must leave no deferred document notification to be emitted by a
    // later layout-only history entry.
    auto layout = session.layoutSettings();
    layout.gridVisible = !layout.gridVisible;
    session.setLayoutSettings(layout);
    QCOMPARE(changed.size(), 4);
    QCOMPARE(session.undoStack()->index(), 1);
    QVERIFY(session.modified());
    session.undoStack()->undo();
    QCOMPARE(changed.size(), 4);
    QCOMPARE(session.undoStack()->index(), 0);
    QVERIFY(!session.modified());
    session.discardRecovery();
}

void SourceStateTests::rejectedDraftKeepsAppliedDocumentAndHistory()
{
    DocumentSession session;
    session.reset(editableDocument());
    const auto original = toZpl(session.document());
    const auto revision = session.revision();
    const auto invalid = u"^XA\n^FO123,"_s;
    session.setPendingSource(invalid);
    QSignalSpy changed(&session, &DocumentSession::documentChanged);
    const auto result = session.applySource();
    QVERIFY(!result.has_value());
    QCOMPARE(session.sourceStatus(), SourceStatus::Rejected);
    QVERIFY(!session.sourceError().isEmpty());
    QCOMPARE(session.pendingSource(), invalid);
    QCOMPARE(toZpl(session.document()), original);
    QCOMPARE(session.revision().instance, revision.instance);
    QCOMPARE(session.revision().document, revision.document);
    QCOMPARE(session.undoStack()->count(), 0);
    QCOMPARE(changed.size(), 0);
    QVERIFY(session.modified());
    session.setPendingSource(replacementSource());
    QCOMPARE(session.sourceStatus(), SourceStatus::Pending);
    QVERIFY(session.sourceError().isEmpty());
    session.discardRecovery();
}

void SourceStateTests::sourceOnlyApplyIsLossless_data()
{
    QTest::addColumn<QString>("source");
    QTest::newRow("unknown-command")
        << u"\r\n^XA\r\n^PW320^LL160^FO10,10^A0N,20,20^FDKeep me^FS^ZZsomething^XZ\r\n"_s;
    QTest::newRow("multiple-labels")
        << u"^XA^PW64^LL32^FO1,1^GB10,10,2^FS^XZ\n^XA^PW128^LL64^FO20,20^GB8,8,2^FS^XZ\n"_s;
}

void SourceStateTests::sourceOnlyApplyIsLossless()
{
    QFETCH(QString, source);
    DocumentSession session;
    session.reset(editableDocument());
    session.setPendingSource(source);
    const auto applied = session.applySource();
    QVERIFY2(applied.has_value(), qPrintable(applied ? QString{} : applied.error()));
    QVERIFY(*applied);
    QCOMPARE(session.sourceStatus(), SourceStatus::Applied);
    QVERIFY(!session.hasPendingSource());
    QVERIFY(!session.canEditVisually());
    QVERIFY(!session.document().visualEditable);
    QCOMPARE(toZpl(session.document()), source);
    session.undoStack()->undo();
    QVERIFY(session.canEditVisually());
    session.undoStack()->redo();
    QCOMPARE(toZpl(session.document()), source);
    session.discardRecovery();
}

void SourceStateTests::staleApplyCannotCrossDocumentOrDraftRevisions()
{
    DocumentSession session;
    session.reset(editableDocument());
    session.setPendingSource(replacementSource());
    const auto firstDocument = session.revision();
    const auto firstDraft = session.sourceRevision();
    const auto newerSource = replacementSource().replace(u"Replacement"_s, u"Newer"_s);
    session.setPendingSource(newerSource);
    QVERIFY(session.sourceRevision() > firstDraft);
    const auto staleDraft = session.applySource(firstDocument, firstDraft);
    QVERIFY(!staleDraft.has_value());
    QCOMPARE(session.pendingSource(), newerSource);
    QCOMPARE(session.sourceStatus(), SourceStatus::Pending);
    QCOMPARE(session.undoStack()->count(), 0);

    session.reset(editableDocument());
    QVERIFY(session.revision().instance != firstDocument.instance);
    session.setPendingSource(replacementSource());
    const auto staleDocument = session.applySource(firstDocument, session.sourceRevision());
    QVERIFY(!staleDocument.has_value());
    QCOMPARE(session.pendingSource(), replacementSource());
    QCOMPARE(session.sourceStatus(), SourceStatus::Pending);
    QCOMPARE(session.undoStack()->count(), 0);
    const auto current = session.applySource(session.revision(), session.sourceRevision());
    QVERIFY(current.has_value());
    QVERIFY(*current);
    session.discardRecovery();
}

void SourceStateTests::pendingSourceBlocksVisualMutationsAndExport()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DocumentSession session;
    session.reset(editableDocument());
    const auto original = toZpl(session.document());
    session.setPendingSource(replacementSource());
    QVERIFY(!session.moveElements({0}, {10, 10}));
    QVERIFY(!session.deleteElements({0}));
    QVERIFY(session.duplicateElements({0}).isEmpty());
    QVERIFY(session.pasteElements(session.document().elements).isEmpty());
    QVERIFY(!session.groupElements({0}));
    QVERIFY(!session.ungroupElements({0}));
    QVERIFY(!session.alignElements({0}, ElementAlignment::Left, AlignmentReference::Label));
    QVERIFY(!session.distributeElements({0}, Qt::Horizontal));
    QVERIFY(!session.exportZpl(directory.filePath(u"template.zpl"_s)).has_value());
    QVERIFY(!session.saveResolved(directory.filePath(u"filled.zpl"_s)).has_value());
    QVERIFY(!session.save(directory.filePath(u"draft.zpl"_s)).has_value());
    QCOMPARE(toZpl(session.document()), original);
    QCOMPARE(session.undoStack()->count(), 0);
    QCOMPARE(session.pendingSource(), replacementSource());
    QCOMPARE(QDir(directory.path()).entryList(QDir::Files).size(), 0);
    session.discardRecovery();
}

void SourceStateTests::rejectedDraftCanBeSavedWithoutEnablingExport()
{
    QTemporaryDir directory;
    DocumentSession session;
    session.reset(editableDocument());
    const auto invalid = u"^XA\n^FO123,"_s;
    session.setPendingSource(invalid);
    QVERIFY(!session.applySource().has_value());
    const auto path = directory.filePath(u"draft.qtzpl"_s);
    QVERIFY(session.save(path).has_value());
    QVERIFY(!session.modified());
    QCOMPARE(session.sourceStatus(), SourceStatus::Rejected);
    QVERIFY(!session.sourceError().isEmpty());
    QVERIFY(!session.exportZpl(directory.filePath(u"template.zpl"_s)).has_value());
    session.setPendingSource(invalid + u' ');
    QVERIFY(session.modified());
    session.setPendingSource(invalid);
    QVERIFY(!session.modified());
    QCOMPARE(session.sourceStatus(), SourceStatus::Pending);
    session.discardRecovery();
}

void SourceStateTests::openingProjectPublishesDraftWithDocument()
{
    QTemporaryDir directory;
    auto document = editableDocument();
    document.width = 777;
    const auto draft = u"^XA\n^FO123,"_s;
    const auto encoded = encodeProject({document, {}, draft});
    QVERIFY(encoded.has_value());
    const auto path = directory.filePath(u"draft.qtzpl"_s);
    QVERIFY(writeBytes(path, *encoded));
    DocumentSession session;
    session.reset(editableDocument());
    const auto before = session.revision();
    bool coherent = true;
    const auto observe = [&] {
        coherent = coherent && session.document().width == 777
            && session.pendingSource() == draft && session.sourceStatus() == SourceStatus::Pending
            && !session.canEditVisually() && !session.modified() && session.filePath() == path
            && session.revision().instance != before.instance;
    };
    connect(&session, &DocumentSession::documentChanged, this, observe);
    connect(&session, &DocumentSession::pendingSourceChanged, this, observe);
    QSignalSpy changed(&session, &DocumentSession::documentChanged);
    QVERIFY(session.open(path).has_value());
    QCOMPARE(changed.size(), 1);
    QVERIFY(coherent);
}

void SourceStateTests::recoveryPublishesDraftWithDocument()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(u"original.qtzpl"_s);
    const auto draft = u"^XA\n^FO123,"_s;
    QString recoveryPath;
    {
        DocumentSession crashed;
        auto document = editableDocument();
        document.width = 777;
        crashed.reset(document);
        QVERIFY(crashed.save(path).has_value());
        crashed.setPendingSource(draft);
        QVERIFY(crashed.flushRecovery().has_value());
        recoveryPath = crashed.recoveryFilePath();
    }
    DocumentSession session;
    session.reset(editableDocument());
    const auto before = session.revision();
    bool coherent = true;
    const auto observe = [&] {
        coherent = coherent && session.document().width == 777
            && session.pendingSource() == draft && session.sourceStatus() == SourceStatus::Pending
            && !session.canEditVisually() && session.modified() && session.filePath() == path
            && session.revision().instance != before.instance;
    };
    connect(&session, &DocumentSession::documentChanged, this, observe);
    connect(&session, &DocumentSession::pendingSourceChanged, this, observe);
    QSignalSpy changed(&session, &DocumentSession::documentChanged);
    QVERIFY(session.recover(recoveryPath).has_value());
    QCOMPARE(changed.size(), 1);
    QVERIFY(coherent);
    session.discardRecovery();
    QVERIFY(!QFileInfo::exists(recoveryPath));
}

void SourceStateTests::resetClearsDraftBeforeNotifyingObservers()
{
    DocumentSession session;
    session.reset(editableDocument());
    session.setPendingSource(u"^XA\n^FO123,"_s);
    QVERIFY(!session.applySource().has_value());
    const auto before = session.revision();
    auto document = editableDocument();
    document.width = 777;
    bool coherent = true;
    const auto observe = [&] {
        coherent = coherent && session.document().width == 777 && !session.hasPendingSource()
            && session.sourceStatus() == SourceStatus::Applied && session.sourceError().isEmpty()
            && session.canEditVisually() && !session.modified()
            && session.revision().instance != before.instance;
    };
    connect(&session, &DocumentSession::documentChanged, this, observe);
    connect(&session, &DocumentSession::pendingSourceChanged, this, observe);
    session.reset(document);
    QVERIFY(coherent);
    QCOMPARE(session.undoStack()->count(), 0);
}

void SourceStateTests::unchangedSourceAddsNoHistory()
{
    DocumentSession session;
    session.reset(editableDocument());
    const auto source = toZpl(session.document());
    const auto revision = session.revision();
    session.setPendingSource(source);
    QVERIFY(!session.hasPendingSource());
    const auto result = session.applySource();
    QVERIFY(result.has_value());
    QVERIFY(!*result);
    QCOMPARE(session.undoStack()->count(), 0);
    QCOMPARE(session.revision().document, revision.document);
    QVERIFY(!session.modified());
    session.setPendingSource(replacementSource());
    session.setPendingSource(source);
    QVERIFY(!session.hasPendingSource());
    QCOMPARE(session.sourceStatus(), SourceStatus::Applied);
    QVERIFY(!session.modified());
    QCOMPARE(session.undoStack()->count(), 0);
}

void SourceStateTests::previewValuesSurviveApplyUndoAndRedo()
{
    DocumentSession session;
    session.reset(editableDocument());
    const auto initial = session.revision();
    const QMap<QString, QString> values{{u"part"_s, u"42"_s}};
    session.setPreviewValues(values);
    QCOMPARE(session.revision().instance, initial.instance);
    QCOMPARE(session.revision().document, initial.document);
    QVERIFY(session.revision().values > initial.values);
    QVERIFY(!session.modified());
    session.setPendingSource(replacementSource());
    QVERIFY(session.applySource().has_value());
    QCOMPARE(session.document().previewValues, values);
    const QMap<QString, QString> newer{{u"part"_s, u"99"_s}};
    session.setPreviewValues(newer);
    session.undoStack()->undo();
    QCOMPARE(session.document().previewValues, newer);
    session.undoStack()->redo();
    QCOMPARE(session.document().previewValues, newer);
    session.discardRecovery();
}

QTEST_MAIN(SourceStateTests)
#include "test_source_state.moc"
