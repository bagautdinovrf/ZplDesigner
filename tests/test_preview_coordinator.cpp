#include "preview_coordinator.hpp"

#include <QSignalSpy>
#include <QtTest>

#include <algorithm>

using namespace Designer;

namespace {
DesignDocument boxDocument()
{
    DesignDocument document;
    document.width = 160;
    document.height = 120;
    auto box = makeElement(ElementType::Box, {12, 18});
    box.width = 24;
    box.height = 16;
    box.thickness = 2;
    document.elements.append(box);
    return document;
}

DocumentRevision revision(quint64 document = 1, quint64 values = 0)
{
    return {QUuid::createUuid(), document, values};
}
} // namespace

class PreviewCoordinatorTests final : public QObject {
    Q_OBJECT
private slots:
    void coalescesRequestsAndKeepsSnapshotGeometry();
    void invalidatesEveryPartOfRequestIdentity();
    void resetRejectsAnotherDocumentWithMatchingCounters();
    void keepsEditorAndPrintVisibilitySeparate();
    void preservesInvalidBarcodeFallback();
    void selectsSourcePagesAndInvalidatesOldPageRequests();
    void diagnosesDraftWithoutReplacingAppliedPreview();
    void retainsDiagnosticIdentityInOriginalSource();
    void newerSnapshotReplacesBackgroundRender();
};

void PreviewCoordinatorTests::coalescesRequestsAndKeepsSnapshotGeometry()
{
    PreviewCoordinator coordinator;
    QSignalSpy changed(&coordinator, &PreviewCoordinator::previewsChanged);
    auto document = boxDocument();
    auto stamp = revision();
    coordinator.setSnapshot(document, stamp);
    const auto oldRequest = coordinator.requestIdentity(PreviewMode::Editor);
    document.elements[0].x = 42;
    ++stamp.document;
    coordinator.setSnapshot(document, stamp);
    document.elements[0].x = 90;
    QVERIFY(coordinator.pending());
    QVERIFY(!coordinator.isCurrent(oldRequest));
    QTRY_COMPARE(changed.count(), 1);
    QVERIFY(!coordinator.pending());
    QCOMPARE(coordinator.editorPreview().bounds.first(), QRect(42, 18, 24, 16));
    QCOMPARE(coordinator.editorPreview().image.pixelColor(42, 18), QColor(Qt::black));
    QCOMPARE(coordinator.editorPreview().image.pixelColor(90, 18), QColor(Qt::white));
    coordinator.refresh();
    coordinator.setSnapshot(boxDocument(), stamp);
    QVERIFY(!coordinator.pending());
    QCOMPARE(changed.count(), 1);
}

void PreviewCoordinatorTests::invalidatesEveryPartOfRequestIdentity()
{
    PreviewCoordinator coordinator;
    auto document = boxDocument();
    auto stamp = revision();
    coordinator.setSnapshot(document, stamp);
    coordinator.refresh();
    auto request = coordinator.requestIdentity(PreviewMode::Editor);
    ++stamp.values;
    document.previewValues.insert(QStringLiteral("batch"), QStringLiteral("123"));
    coordinator.setSnapshot(document, stamp);
    QVERIFY(!coordinator.isCurrent(request));
    QVERIFY(coordinator.editorPreview().image.isNull());
    QVERIFY(coordinator.editorPreview().bounds.isEmpty());
    QVERIFY(coordinator.printPreview().image.isNull());
    request = coordinator.requestIdentity(PreviewMode::Editor);
    document.dpi = 300;
    coordinator.setSnapshot(document, stamp);
    QVERIFY(!coordinator.isCurrent(request));
    QCOMPARE(coordinator.dpi(), 300);
    request = coordinator.requestIdentity(PreviewMode::Editor);
    ++stamp.document;
    coordinator.setSnapshot(document, stamp);
    QVERIFY(!coordinator.isCurrent(request));
    QVERIFY(coordinator.isCurrent(coordinator.requestIdentity(PreviewMode::Editor)));
}

void PreviewCoordinatorTests::resetRejectsAnotherDocumentWithMatchingCounters()
{
    PreviewCoordinator coordinator;
    auto stamp = revision();
    coordinator.setSnapshot(boxDocument(), stamp);
    const auto oldRequest = coordinator.requestIdentity(PreviewMode::Editor);
    stamp.instance = QUuid::createUuid();
    auto replacement = boxDocument();
    replacement.elements[0].x = 72;
    coordinator.setSnapshot(replacement, stamp);
    QVERIFY(!coordinator.isCurrent(oldRequest));
    coordinator.refresh();
    QCOMPARE(coordinator.editorPreview().bounds.first().x(), 72);
}

void PreviewCoordinatorTests::keepsEditorAndPrintVisibilitySeparate()
{
    PreviewCoordinator coordinator;
    auto document = boxDocument();
    document.elements[0].printable = false;
    auto printOnly = document.elements[0];
    printOnly.id = QUuid::createUuid();
    printOnly.x = 72;
    printOnly.printable = true;
    printOnly.editorVisible = false;
    document.elements.append(printOnly);
    coordinator.setSnapshot(document, revision());
    coordinator.refresh();
    QVERIFY(coordinator.printError().isEmpty());
    QCOMPARE(coordinator.editorPreview().bounds.size(), 2);
    QCOMPARE(coordinator.editorPreview().image.pixelColor(12, 18), QColor(Qt::black));
    QCOMPARE(coordinator.editorPreview().image.pixelColor(72, 18), QColor(Qt::white));
    QCOMPARE(coordinator.printPreview().image.pixelColor(12, 18), QColor(Qt::white));
    QCOMPARE(coordinator.printPreview().image.pixelColor(72, 18), QColor(Qt::black));
    QCOMPARE(coordinator.pagePreview().image, coordinator.printPreview().image);
}

void PreviewCoordinatorTests::preservesInvalidBarcodeFallback()
{
    PreviewCoordinator coordinator;
    auto document = boxDocument();
    auto barcode = makeElement(ElementType::DataMatrix, {60, 24});
    barcode.gs1 = true;
    barcode.text = QStringLiteral("{{missing}}");
    document.elements.append(barcode);
    coordinator.setSnapshot(document, revision());
    coordinator.refresh();
    QVERIFY(!coordinator.printError().isEmpty());
    QVERIFY(coordinator.printPreview().image.isNull());
    QVERIFY(!coordinator.editorPreview().image.isNull());
    QCOMPARE(coordinator.editorPreview().bounds.size(), 2);
    QVERIFY(coordinator.editorPreview().elementErrors.contains(1));
    QVERIFY(!coordinator.editorPreview().bounds[1].isEmpty());
    QCOMPARE(coordinator.editorPreview().image.pixelColor(12, 18), QColor(Qt::black));
}

void PreviewCoordinatorTests::selectsSourcePagesAndInvalidatesOldPageRequests()
{
    PreviewCoordinator coordinator;
    auto document = boxDocument();
    document.visualEditable = false;
    document.originalZpl = QStringLiteral(
        "^XA^PW160^LL120^FO12,18^GB24,16,2^FS^XZ\r\n"
        "^XA^PW80^LL60^FO5,8^GB20,12,2^FS^XZ");
    const auto stamp = revision();
    coordinator.setSnapshot(document, stamp);
    coordinator.refresh();
    QCOMPARE(coordinator.pageCount(), 2);
    QCOMPARE(coordinator.pagePreview().image.size(), QSize(160, 120));
    const auto firstPageRequest = coordinator.requestIdentity(PreviewMode::Print);
    const auto editorRequest = coordinator.requestIdentity(PreviewMode::Editor);
    coordinator.selectPage(1);
    QCOMPARE(coordinator.page(), 1);
    QCOMPARE(coordinator.pagePreview().image.size(), QSize(80, 60));
    QCOMPARE(coordinator.editorPreview().image.size(), QSize(160, 120));
    QVERIFY(coordinator.editorPreview().bounds.isEmpty());
    QVERIFY(!coordinator.isCurrent(firstPageRequest));
    QVERIFY(coordinator.isCurrent(editorRequest));
    coordinator.setSnapshot(boxDocument(), revision());
    coordinator.refresh();
    QCOMPARE(coordinator.page(), 0);
    QCOMPARE(coordinator.pageCount(), 1);
}

void PreviewCoordinatorTests::diagnosesDraftWithoutReplacingAppliedPreview()
{
    PreviewCoordinator coordinator;
    coordinator.setSnapshot(boxDocument(), revision());
    coordinator.refresh();
    const auto request = coordinator.requestIdentity(PreviewMode::Editor);
    const auto image = coordinator.editorPreview().image;
    DesignDocument draft;
    draft.visualEditable = false;
    draft.originalZpl = QStringLiteral("^XA^FO10,10^GB20,20,1^FS^ZZignored^XZ");
    const auto diagnostics = coordinator.sourceDiagnostics(draft);
    QVERIFY(!diagnostics.isEmpty());
    QVERIFY(coordinator.isCurrent(request));
    QCOMPARE(coordinator.editorPreview().image, image);
}

void PreviewCoordinatorTests::retainsDiagnosticIdentityInOriginalSource()
{
    DesignDocument document;
    document.visualEditable = false;
    document.originalZpl = QStringLiteral(
        "^XA^PW160^LL120^CI28^FO5,5^A0N,10,8^FDЖ😀^FS^XZ\r\n"
        "^xa^PW80^LL60^FO5,5^A@N,10,8,E:missing.ttf^FDtest^FS^ZZretained^xz");
    const auto diagnostics = sourceDiagnostics(document, 1);
    const auto unknownOffset = document.originalZpl.indexOf(QStringLiteral("^ZZ"));
    const auto unknown = std::ranges::find_if(diagnostics, [unknownOffset](const SourceDiagnostic& diagnostic) {
        return diagnostic.offset == unknownOffset;
    });
    QVERIFY(unknown != diagnostics.end());
    QVERIFY(!unknown->detail.code.isEmpty());
    QCOMPARE(unknown->detail.offset, unknownOffset);
    QVERIFY(unknown->detail.labelIndex.has_value());
    QCOMPARE(*unknown->detail.labelIndex, 1);
    QVERIFY(unknown->detail.sourceSpan.has_value());
    QCOMPARE(document.originalZpl.mid(unknown->detail.sourceSpan->start, unknown->detail.sourceSpan->length),
             QStringLiteral("^ZZretained"));
    for (qsizetype i = 0; i < diagnostics.size(); ++i)
        for (qsizetype j = i + 1; j < diagnostics.size(); ++j)
            QVERIFY(diagnostics[i].detail.code != diagnostics[j].detail.code
                || diagnostics[i].detail.offset != diagnostics[j].detail.offset
                || diagnostics[i].detail.message != diagnostics[j].detail.message);
}

void PreviewCoordinatorTests::newerSnapshotReplacesBackgroundRender()
{
    PreviewCoordinator coordinator;
    QSignalSpy changed(&coordinator, &PreviewCoordinator::previewsChanged);
    auto document = boxDocument();
    auto stamp = revision();
    coordinator.setSnapshot(document, stamp);
    // Let the debounce hand the first snapshot to the worker thread.
    QTest::qWait(150);
    document.elements[0].x = 60;
    ++stamp.document;
    coordinator.setSnapshot(document, stamp);
    QVERIFY(coordinator.pending());
    QTRY_VERIFY(!coordinator.pending());
    QCOMPARE(coordinator.editorPreview().bounds.first(), QRect(60, 18, 24, 16));
    QCOMPARE(coordinator.editorPreview().image.pixelColor(60, 18), QColor(Qt::black));
    QVERIFY(changed.count() <= 2);
    // Nothing from the replaced render arrives afterwards.
    QTest::qWait(150);
    QCOMPARE(coordinator.editorPreview().bounds.first(), QRect(60, 18, 24, 16));
}

QTEST_MAIN(PreviewCoordinatorTests)
#include "test_preview_coordinator.moc"
