#include "document_session.hpp"
#include "document_operations.hpp"
#include "project_codec.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
DesignDocument simpleDocument()
{
    DesignDocument document;
    auto text = makeElement(ElementType::Text, {24, 32});
    text.text = u"Original"_s;
    document.elements = {text};
    return document;
}
} // namespace

class SessionOperationTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void barcodeChangeUsesOneSnapshotAndRestoresSourceMetadata();
    void noOpAndRejectedRequestsKeepHistoryAndRevision();
    void lockedSelectionRejectsWholeRequest();
    void resizeRejectsStaleGeometry_data();
    void resizeRejectsStaleGeometry();
    void pendingRejectedAndSourceOnlyStatesBlockOperations();
    void intermediateGs1TextStaysUndoableAndPortable();
    void reorderUndoKeepsElementAndGroupIds();
    void scaledLabelStepsFromBaseAndMovesGuides();
};

void SessionOperationTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(u"QtZplTests"_s);
    QCoreApplication::setApplicationName(u"session-operations-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

void SessionOperationTests::cleanupTestCase()
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

void SessionOperationTests::barcodeChangeUsesOneSnapshotAndRestoresSourceMetadata()
{
    const auto source = u"^XA\n^PW600\n^LL300\n^FO8,8^GB8,8,1^FS\n^FXKeep this field note^FS\n"
                        u"^FO24,32^BY2,3,80^BCN,80,Y,N,N,A^FD12345678^FS\n^XZ\n"_s;
    const auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY(imported->visualEditable);
    DocumentSession session;
    session.reset(*imported);
    const auto before = session.document().elements.last();
    const auto revision = session.revision();
    QSignalSpy changed(&session, &DocumentSession::documentChanged);
    const auto result = session.execute(ChangeBarcodeType{before.id, ElementType::Code93}, u"Barcode type"_s);
    QVERIFY(result.has_value());
    QVERIFY(*result);
    QCOMPARE(session.undoStack()->count(), 1);
    QCOMPARE(changed.size(), 1);
    QVERIFY(session.modified());
    QCOMPARE(session.revision().instance, revision.instance);
    QVERIFY(session.revision().document > revision.document);
    const auto& after = session.document().elements.last();
    QCOMPARE(after.id, before.id);
    QCOMPARE(after.text, before.text);
    QCOMPARE(after.sourcePrefix, before.sourcePrefix);
    QCOMPARE(after.sourceZpl, before.sourceZpl);
    QCOMPARE(after.sourceSignature, before.sourceSignature);
    QCOMPARE(after.sourceState, before.sourceState);
    const auto output = toZpl(session.document());
    QVERIFY(output.contains(u"^FXKeep this field note^FS"));
    QVERIFY(output.contains(u"^BA"));
    session.undoStack()->undo();
    QCOMPARE(toZpl(session.document()), source);
    QCOMPARE(session.document().elements.last().id, before.id);
    QVERIFY(!session.modified());
    session.undoStack()->redo();
    QCOMPARE(toZpl(session.document()), output);
    QCOMPARE(session.document().elements.last().id, before.id);
    session.discardRecovery();
}

void SessionOperationTests::noOpAndRejectedRequestsKeepHistoryAndRevision()
{
    DocumentSession session;
    session.reset(simpleDocument());
    const auto& element = session.document().elements.front();
    const auto id = element.id;
    const auto original = toZpl(session.document());
    const auto revision = session.revision();
    QSignalSpy changed(&session, &DocumentSession::documentChanged);
    const auto unchanged = session.execute(UpdateText{id, element.text, {}}, u"Unchanged"_s);
    QVERIFY(unchanged.has_value());
    QVERIFY(!*unchanged);
    ElementProperties properties;
    properties.text = u"Must not survive rejection"_s;
    properties.x = 5000;
    QVERIFY(!session.execute(UpdateElementProperties{id, properties}, u"Invalid property"_s).has_value());
    QVERIFY(!session.execute(UpdateText{QUuid::createUuid(), u"Missing"_s, {}}, u"Missing"_s).has_value());
    QVERIFY(!session.execute(ChangeBarcodeType{id, ElementType::QrCode}, u"Ineligible"_s).has_value());
    QCOMPARE(toZpl(session.document()), original);
    QCOMPARE(session.revision().document, revision.document);
    QCOMPARE(session.undoStack()->count(), 0);
    QCOMPARE(changed.size(), 0);
    QVERIFY(!session.modified());
}

void SessionOperationTests::lockedSelectionRejectsWholeRequest()
{
    auto document = simpleDocument();
    auto locked = makeElement(ElementType::Text, {24, 100});
    locked.locked = true;
    document.elements.append(locked);
    DocumentSession session;
    session.reset(document);
    SelectionPropertyValues values;
    values.fontHeight = 70;
    const QList<QUuid> ids{document.elements[0].id, locked.id};
    const auto rejected = session.execute(UpdateSelectionProperties{ids, values}, u"Selection"_s);
    QVERIFY(!rejected.has_value());
    QCOMPARE(session.document().elements[0].fontHeight, document.elements[0].fontHeight);
    QCOMPARE(session.document().elements[1].fontHeight, locked.fontHeight);
    QCOMPARE(session.undoStack()->count(), 0);
    const auto unlocked = session.execute(SetElementFlags{{locked.id}, LayerFlag::Locked, false}, u"Unlock"_s);
    QVERIFY(unlocked.has_value());
    QVERIFY(*unlocked);
    const auto changed = session.execute(UpdateSelectionProperties{ids, values}, u"Selection"_s);
    QVERIFY(changed.has_value());
    QVERIFY(*changed);
    QCOMPARE(session.undoStack()->count(), 2);
    QCOMPARE(session.document().elements[0].fontHeight, 70);
    QCOMPARE(session.document().elements[1].fontHeight, 70);
    session.undoStack()->undo();
    QCOMPARE(session.document().elements[0].fontHeight, document.elements[0].fontHeight);
    QCOMPARE(session.document().elements[1].fontHeight, locked.fontHeight);
    QVERIFY(!session.document().elements[1].locked);
    session.undoStack()->undo();
    QVERIFY(session.document().elements[1].locked);
    QVERIFY(!session.modified());
    session.discardRecovery();
}

void SessionOperationTests::resizeRejectsStaleGeometry_data()
{
    QTest::addColumn<int>("change");
    QTest::newRow("applied-document") << 0;
    QTest::newRow("preview-values") << 1;
    QTest::newRow("document-instance") << 2;
}

void SessionOperationTests::resizeRejectsStaleGeometry()
{
    QFETCH(int, change);
    DesignDocument document;
    auto box = makeElement(ElementType::Box, {24, 32});
    box.width = 80;
    box.height = 40;
    document.elements = {box};
    DocumentSession session;
    session.reset(document);
    const auto geometry = session.revision();
    if (change == 0) {
        QVERIFY(session.execute(RenameElement{box.id, u"Renamed"_s}, u"Rename"_s).has_value());
    } else if (change == 1) {
        session.setPreviewValues({{u"part"_s, u"42"_s}});
    } else {
        session.reset(document);
    }
    const int historyCount = session.undoStack()->count();
    const auto stale = session.execute(ResizeElement{box.id, {160, 80}, {80, 40}, geometry, {}}, u"Resize"_s);
    QVERIFY(!stale.has_value());
    QCOMPARE(session.document().elements[0].width, 80);
    QCOMPARE(session.document().elements[0].height, 40);
    QCOMPARE(session.undoStack()->count(), historyCount);
    const auto current = session.execute(ResizeElement{box.id, {160, 80}, {80, 40}, session.revision(), {}}, u"Resize"_s);
    QVERIFY2(current.has_value(), qPrintable(current ? QString{} : current.error()));
    QVERIFY(*current);
    QCOMPARE(session.document().elements[0].width, 160);
    QCOMPARE(session.document().elements[0].height, 80);
    QCOMPARE(session.undoStack()->count(), historyCount + 1);
    session.undoStack()->undo();
    QCOMPARE(session.document().elements[0].width, 80);
    QCOMPARE(session.document().elements[0].height, 40);
    session.discardRecovery();
}

void SessionOperationTests::pendingRejectedAndSourceOnlyStatesBlockOperations()
{
    DocumentSession session;
    session.reset(simpleDocument());
    const auto id = session.document().elements[0].id;
    const auto original = toZpl(session.document());
    const EditOperation operation = UpdateText{id, u"Do not apply"_s, {}};
    session.setPendingSource(u"^XA\n^FO123,"_s);
    QVERIFY(!session.execute(operation, u"Pending"_s).has_value());
    QVERIFY(!session.applySource().has_value());
    QCOMPARE(session.sourceStatus(), SourceStatus::Rejected);
    QVERIFY(!session.execute(operation, u"Rejected"_s).has_value());
    QCOMPARE(toZpl(session.document()), original);
    QCOMPARE(session.undoStack()->count(), 0);
    const auto sourceOnly = u"^XA^PW320^LL160^ZZpreserve^XZ"_s;
    session.setPendingSource(sourceOnly);
    QVERIFY(session.applySource().has_value());
    QVERIFY(!session.canEditVisually());
    const int historyCount = session.undoStack()->count();
    QVERIFY(!session.execute(AddElement{makeElement(ElementType::Text)}, u"Source-only"_s).has_value());
    QCOMPARE(toZpl(session.document()), sourceOnly);
    QCOMPARE(session.undoStack()->count(), historyCount);
    session.discardRecovery();
}

void SessionOperationTests::intermediateGs1TextStaysUndoableAndPortable()
{
    DesignDocument document;
    auto matrix = makeElement(ElementType::DataMatrix, {24, 32});
    matrix.gs1 = true;
    matrix.text = u"(01)04601234567893(21)123"_s;
    document.elements = {matrix};
    DocumentSession session;
    session.reset(document);
    const auto incomplete = u"(01)123"_s;
    const auto changed = session.execute(UpdateText{matrix.id, incomplete, true}, u"Edit GS1"_s);
    QVERIFY(changed.has_value());
    QVERIFY(*changed);
    QCOMPARE(session.document().elements[0].text, incomplete);
    QVERIFY(session.canEditVisually());
    QVERIFY(!validateForExport(session.document()).has_value());
    QCOMPARE(session.undoStack()->count(), 1);
    const auto encoded = encodeProject({session.document(), session.layoutSettings(), {}});
    QVERIFY2(encoded.has_value(), qPrintable(encoded ? QString{} : encoded.error()));
    const auto decoded = decodeProject(*encoded);
    QVERIFY(decoded.has_value());
    QCOMPARE(decoded->document.elements[0].text, incomplete);
    QCOMPARE(decoded->document.elements[0].id, matrix.id);
    session.undoStack()->undo();
    QCOMPARE(session.document().elements[0].text, matrix.text);
    session.undoStack()->redo();
    QCOMPARE(session.document().elements[0].text, incomplete);
    session.discardRecovery();
}

void SessionOperationTests::reorderUndoKeepsElementAndGroupIds()
{
    DesignDocument document;
    const auto group = QUuid::createUuid();
    for (int index = 0; index < 4; ++index) {
        auto box = makeElement(ElementType::Box, {20 + index * 40, 20});
        if (index < 2) box.groupId = group;
        document.elements.append(box);
    }
    DocumentSession session;
    session.reset(document);
    const QList<QUuid> ids{document.elements[0].id, document.elements[1].id};
    const auto changed = session.execute(ReorderElements{ids, 200}, u"Bring front"_s);
    QVERIFY(changed.has_value());
    QVERIFY(*changed);
    QCOMPARE(session.document().elements[2].id, ids[0]);
    QCOMPARE(session.document().elements[3].id, ids[1]);
    QCOMPARE(session.document().elements[2].groupId, group);
    QCOMPARE(session.document().elements[3].groupId, group);
    QCOMPARE(session.undoStack()->count(), 1);
    const auto noOp = session.execute(ReorderElements{ids, 200}, u"Already front"_s);
    QVERIFY(noOp.has_value());
    QVERIFY(!*noOp);
    QCOMPARE(session.undoStack()->count(), 1);
    session.undoStack()->undo();
    QCOMPARE(session.document().elements[0].id, ids[0]);
    QCOMPARE(session.document().elements[1].id, ids[1]);
    session.undoStack()->redo();
    QCOMPARE(session.document().elements[2].id, ids[0]);
    QCOMPARE(session.document().elements[3].id, ids[1]);
    session.discardRecovery();
}

void SessionOperationTests::scaledLabelStepsFromBaseAndMovesGuides()
{
    DesignDocument document;
    document.width = 400; document.height = 300;
    document.elements = {makeElement(ElementType::Box, {10, 20})};
    DocumentSession session;
    session.reset(document);
    auto layout = session.layoutSettings();
    layout.verticalGuides = {100};
    layout.horizontalGuides = {150};
    session.setLayoutSettings(layout);
    const auto scale = [&](int width, int height, int dpi) {
        UpdateLabelSettings settings{width, height, dpi};
        settings.scaleContent = true;
        return session.execute(settings, u"Scale"_s);
    };
    const auto box = [&]() -> const Element& { return session.document().elements.front(); };

    // Single-dot steps would round back to x = 10 if each scaled the last one.
    // Sixty steps stay below the 80-command history limit.
    for (int width = 401; width <= 460; ++width) QVERIFY(scale(width, 300, 203).value_or(false));
    QCOMPARE(box().x, qRound(10 * 460.0 / 400));
    QCOMPARE(box().width, qRound(250 * 460.0 / 400) - box().x);
    QCOMPARE(session.layoutSettings().verticalGuides, QList<int>{115});
    QCOMPARE(session.layoutSettings().horizontalGuides, QList<int>{150});
    const auto same = scale(460, 300, 203);
    QVERIFY(same && !*same);

    // Returning to the base restores every dot, guides included.
    QVERIFY(scale(400, 300, 203).value_or(false));
    QCOMPARE(box().x, 10);
    QCOMPARE(box().width, document.elements.front().width);
    QCOMPARE(session.layoutSettings().verticalGuides, QList<int>{100});

    // Elements and guides change in one history step.
    const int steps = session.undoStack()->count();
    QVERIFY(scale(800, 600, 300).value_or(false));
    QCOMPARE(session.undoStack()->count(), steps + 1);
    QCOMPARE(session.layoutSettings().horizontalGuides, QList<int>{300});
    session.undoStack()->undo();
    QCOMPARE(session.document().width, 400);
    QCOMPARE(session.document().dpi, 203);
    QCOMPARE(box().x, 10);
    QCOMPARE(session.layoutSettings().horizontalGuides, QList<int>{150});

    // Another edit starts a new base, so the next scaling keeps it.
    QVERIFY(session.moveElements({0}, {5, 0}));
    QVERIFY(scale(800, 600, 203).value_or(false));
    QCOMPARE(box().x, 30);
    session.discardRecovery();
}

QTEST_MAIN(SessionOperationTests)
#include "test_session_operations.moc"
