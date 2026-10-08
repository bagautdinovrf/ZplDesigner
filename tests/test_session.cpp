#include "document_session.hpp"
#include "preset_catalog.hpp"
#include "data_matrix_values.hpp"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

namespace {
Designer::DesignDocument simpleDocument()
{
    Designer::DesignDocument document;
    auto text = Designer::makeElement(Designer::ElementType::Text, {24, 32});
    text.text = QStringLiteral("PART 42");
    document.elements.append(text);
    return document;
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

Designer::CanvasLayoutSettings customLayout()
{
    Designer::CanvasLayoutSettings settings;
    settings.gridVisible = false;
    settings.snapEnabled = false;
    settings.rulersVisible = false;
    settings.guidesVisible = false;
    settings.marginsVisible = true;
    settings.gridStep = 13;
    settings.margin = 27;
    settings.unit = Designer::CanvasUnit::Dots;
    settings.horizontalGuides = {0, 40, 4096};
    settings.verticalGuides = {15, 99};
    return settings;
}
} // namespace

class SessionTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void undoRedoRestoresSnapshotsAndCleanState();
    void saveAndReopen();
    void preserveSourceAndUndoItsReplacement();
    void failedSaveKeepsDirtyDocument();
    void invalidInputKeepsCurrentDocument();
    void presetCatalogRoundTrip();
    void unsafeExportLeavesSavedFileUntouched();
    void groupMovePreservesSpacingAndUsesOneUndo();
    void groupDuplicateAndDeleteAreAtomic();
    void alignmentUsesRenderedFootprint_data();
    void alignmentUsesRenderedFootprint();
    void selectionAlignmentAndDistribution();
    void clippedShapesUseFullGeometryAndKeepNegativeOtherAxis();
    void groupOperationsRejectSourceModeAndExcessCopies();
    void filledExportPreservesTemplateAndSession();
    void layoutSettingsHaveIndependentUndo();
    void layoutGuidesUseOneCanonicalRepresentation();
    void layoutRoundTripAndSaveAs();
    void legacyLayoutMigratesWithoutChangingSidecar();
    void localLayoutDoesNotRequireSidecarAccess();
    void missingLayoutRestoresDefaults();
    void corruptOrMismatchedLayoutKeepsZplUsable();
    void layoutValidationRejectsInvalidMetadata();
    void failedLayoutSaveKeepsZplAndDirtyState();
    void layoutEditsPreserveUnsupportedSourceBytes();
};

void SessionTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("QtZplTests"));
    QCoreApplication::setApplicationName(QStringLiteral("session-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

void SessionTests::cleanupTestCase()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QDir layouts(QDir(directory).filePath(QStringLiteral("layouts")));
    // Only remove this run's known metadata files and empty directories.
    // Never recursively delete a user's application data directory.
    for (const auto& entry : layouts.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.isDir()) QVERIFY(QDir().rmdir(entry.absoluteFilePath()));
        else QVERIFY(QFile::remove(entry.absoluteFilePath()));
    }
    if (layouts.exists()) QVERIFY(QDir().rmdir(layouts.absolutePath()));
    if (QDir(directory).exists()) QVERIFY(QDir().rmdir(directory));
    QStandardPaths::setTestModeEnabled(false);
}

void SessionTests::undoRedoRestoresSnapshotsAndCleanState()
{
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    QSignalSpy changed(&session, &Designer::DocumentSession::documentChanged);
    QSignalSpy modified(&session, &Designer::DocumentSession::modifiedChanged);
    auto moved = session.document();
    moved.elements[0].x = 120;
    session.commit(moved, QStringLiteral("Move"));
    QCOMPARE(session.document().elements[0].x, 120);
    QVERIFY(session.modified());
    QCOMPARE(changed.count(), 1);
    QVERIFY(!modified.isEmpty());
    QCOMPARE(modified.last()[0].toBool(), true);
    session.undoStack()->undo();
    QCOMPARE(session.document().elements[0].x, 24);
    QVERIFY(!session.modified());
    session.undoStack()->redo();
    QCOMPARE(session.document().elements[0].x, 120);
    QVERIFY(session.modified());

    // A caller retaining its value must not mutate an undo snapshot.
    moved.elements[0].x = 900;
    session.undoStack()->undo();
    session.undoStack()->redo();
    QCOMPARE(session.document().elements[0].x, 120);
    session.reset(simpleDocument());
    QVERIFY(!session.undoStack()->canUndo());
    QVERIFY(!session.modified());
}

void SessionTests::saveAndReopen()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("label.zpl"));
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    auto moved = session.document();
    moved.elements[0].x = 96;
    session.commit(moved, QStringLiteral("Move"));
    const auto saved = session.save(path);
    QVERIFY2(saved.has_value(), saved ? "" : qPrintable(saved.error()));
    QVERIFY(!session.modified());
    QCOMPARE(session.filePath(), path);
    session.undoStack()->undo();
    QVERIFY(session.modified());
    session.undoStack()->redo();
    QVERIFY(!session.modified());

    Designer::DocumentSession reopened;
    const auto opened = reopened.open(path);
    QVERIFY2(opened.has_value(), opened ? "" : qPrintable(opened.error()));
    QVERIFY(!reopened.modified());
    QCOMPARE(reopened.filePath(), path);
    QCOMPARE(Designer::toZpl(reopened.document()), Designer::toZpl(session.document()));
    QVERIFY(!reopened.undoStack()->canUndo());
}

void SessionTests::preserveSourceAndUndoItsReplacement()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString label = Designer::toZpl(simpleDocument());
    const QString source = label + QStringLiteral("\r\n") + label;
    const auto parsed = Designer::fromZpl(source);
    QVERIFY2(parsed.has_value(), parsed ? "" : qPrintable(parsed.error()));
    QVERIFY(!parsed->visualEditable);

    Designer::DocumentSession session;
    session.reset(simpleDocument());
    session.commit(*parsed, QStringLiteral("Apply source"), true);
    QCOMPARE(Designer::toZpl(session.document()), source);
    const QString path = directory.filePath(QStringLiteral("original.zpl"));
    const auto saved = session.save(path);
    QVERIFY(saved.has_value());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), source.toUtf8());
    file.close();

    session.commit(simpleDocument(), QStringLiteral("Replace"));
    QVERIFY(session.document().originalZpl.isEmpty());
    session.undoStack()->undo();
    QCOMPARE(Designer::toZpl(session.document()), source);
    QVERIFY(!session.modified());

    const auto opened = session.open(path);
    QVERIFY(opened.has_value());
    QCOMPARE(session.document().originalZpl, source);
    QVERIFY(!session.document().visualEditable);
}

void SessionTests::failedSaveKeepsDirtyDocument()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    auto changed = session.document();
    changed.elements[0].text = QStringLiteral("CHANGED");
    session.commit(changed, QStringLiteral("Edit"));
    const auto result = session.save(directory.filePath(QStringLiteral("missing/label.zpl")));
    QVERIFY(!result.has_value());
    QVERIFY(session.modified());
    QVERIFY(session.filePath().isEmpty());
    QCOMPARE(session.document().elements[0].text, QStringLiteral("CHANGED"));
}

void SessionTests::invalidInputKeepsCurrentDocument()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    const QString original = Designer::toZpl(session.document());
    const QString path = directory.filePath(QStringLiteral("bad.zpl"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray::fromHex("5e5841ff5e585a")), 7);
    file.close();
    QVERIFY(!session.open(path));
    QCOMPARE(Designer::toZpl(session.document()), original);
    QVERIFY(session.filePath().isEmpty());

    // A truncated multi-byte sequence at EOF must also be rejected.
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray::fromHex("5e58415e585ae282")), 8);
    file.close();
    QVERIFY(!session.open(path));
    QCOMPARE(Designer::toZpl(session.document()), original);

    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.resize(8 * 1024 * 1024 + 1));
    file.close();
    QVERIFY(!session.open(path));
    QCOMPARE(Designer::toZpl(session.document()), original);
    QVERIFY(!session.modified());
}

void SessionTests::presetCatalogRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("catalog.ini"));
    const QString first = Designer::toZpl(simpleDocument());
    const QString second = first + QStringLiteral("\r\n") + first;
    const QString firstName = QStringLiteral("Деталь, A\\B");
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.beginWriteArray(QStringLiteral("Presets"), 2);
        settings.setArrayIndex(0);
        settings.setValue(QStringLiteral("name"), firstName);
        settings.setValue(QStringLiteral("template"), first);
        settings.setArrayIndex(1);
        settings.setValue(QStringLiteral("name"), QStringLiteral("Two labels"));
        settings.setValue(QStringLiteral("template"), second);
        settings.endArray();
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
    }
    const auto presets = Designer::readPresetCatalog(path);
    QVERIFY2(presets.has_value(), presets ? "" : qPrintable(presets.error()));
    QCOMPARE(presets->size(), 2);
    QCOMPARE(presets->at(0).name, firstName);
    QCOMPARE(presets->at(0).source, first);
    QCOMPARE(presets->at(1).source, second);
    const auto parsed = Designer::fromZpl(presets->at(1).source);
    QVERIFY(parsed.has_value());
    QCOMPARE(Designer::toZpl(*parsed), second);
    QVERIFY(!parsed->visualEditable);
}

void SessionTests::unsafeExportLeavesSavedFileUntouched()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("label.zpl"));
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    QVERIFY(session.save(path));
    QFile originalFile(path);
    QVERIFY(originalFile.open(QIODevice::ReadOnly));
    const QByteArray savedBytes = originalFile.readAll();
    originalFile.close();
    QVERIFY(!savedBytes.isEmpty());

    auto tooMany = session.document();
    while (tooMany.elements.size() <= 200)
        tooMany.elements.append(Designer::makeElement(Designer::ElementType::Text));
    session.commit(std::move(tooMany), QStringLiteral("Too many elements"));
    QVERIFY(!session.save(path));
    QVERIFY(session.modified());
    QCOMPARE(session.filePath(), path);
    QVERIFY(originalFile.open(QIODevice::ReadOnly));
    QCOMPARE(originalFile.readAll(), savedBytes);
    originalFile.close();
    session.undoStack()->undo();
    QVERIFY(!session.modified());

    auto oversized = session.document();
    oversized.elements.clear();
    oversized.visualEditable = false;
    // Fewer than 8 Mi UTF-16 characters, but more than 8 MiB when saved as UTF-8.
    oversized.originalZpl = QStringLiteral("^XA^FX")
        + QString(4 * 1024 * 1024 + 1, QChar(u'Ж')) + QStringLiteral("^FS^XZ");
    session.commit(std::move(oversized), QStringLiteral("Oversized UTF-8 source"), true);
    QVERIFY(!session.save(path));
    QVERIFY(session.modified());
    QCOMPARE(session.filePath(), path);
    QVERIFY(originalFile.open(QIODevice::ReadOnly));
    QCOMPARE(originalFile.readAll(), savedBytes);
    originalFile.close();
    session.undoStack()->undo();
    QVERIFY(!session.modified());
}

void SessionTests::groupMovePreservesSpacingAndUsesOneUndo()
{
    Designer::DocumentSession session;
    auto document = simpleDocument();
    document.elements[0].x = 10;
    document.elements.append(Designer::makeElement(Designer::ElementType::Box, {50, 90}));
    session.reset(document);
    QSignalSpy changed(&session, &Designer::DocumentSession::documentChanged);
    QVERIFY(session.moveElements({0, 1, 0, -1, 99}, {-40, 15}));
    QCOMPARE(changed.size(), 1);
    QCOMPARE(session.undoStack()->count(), 1);
    QCOMPARE(session.document().elements[0].x, 0);
    QCOMPARE(session.document().elements[1].x, 40);
    QCOMPARE(session.document().elements[0].y, 47);
    QCOMPARE(session.document().elements[1].y, 105);
    QVERIFY(!session.moveElements({0, 1}, {-1, 0}));
    QCOMPARE(session.undoStack()->count(), 1);
    session.undoStack()->undo();
    QCOMPARE(Designer::toZpl(session.document()), Designer::toZpl(document));
    QVERIFY(!session.modified());
}

void SessionTests::groupDuplicateAndDeleteAreAtomic()
{
    Designer::DocumentSession session;
    auto document = simpleDocument();
    document.elements.append(Designer::makeElement(Designer::ElementType::Box, {90, 80}));
    session.reset(document);
    QCOMPARE(session.duplicateElements({1, 0, 1}), QList<int>({2, 3}));
    QCOMPARE(session.document().elements.size(), 4);
    QCOMPARE(session.undoStack()->count(), 1);
    QCOMPARE(session.document().elements[2].text, document.elements[0].text);
    QCOMPARE(session.document().elements[2].x, document.elements[0].x + 16);
    QCOMPARE(session.document().elements[3].y, document.elements[1].y + 16);
    QVERIFY(session.document().elements[2].id != document.elements[0].id);
    QVERIFY(session.document().elements[3].id != document.elements[1].id);
    const auto copiedId = session.document().elements[2].id;
    session.undoStack()->undo();
    QCOMPARE(session.document().elements.size(), 2);
    session.undoStack()->redo();
    QCOMPARE(session.document().elements[2].id, copiedId);
    QVERIFY(session.deleteElements({3, 1, 3, -1, 99}));
    QCOMPARE(session.document().elements.size(), 2);
    QCOMPARE(session.document().elements[0].id, document.elements[0].id);
    QCOMPARE(session.document().elements[1].id, copiedId);
    QCOMPARE(session.undoStack()->count(), 2);
    session.undoStack()->undo();
    QCOMPARE(session.document().elements.size(), 4);
}

void SessionTests::alignmentUsesRenderedFootprint_data()
{
    QTest::addColumn<int>("alignment");
    QTest::addColumn<QPoint>("expected");
    using enum Designer::ElementAlignment;
    QTest::newRow("left") << static_cast<int>(Left) << QPoint(-3, 50);
    QTest::newRow("horizontal-center") << static_cast<int>(HorizontalCenter) << QPoint(157, 50);
    QTest::newRow("right") << static_cast<int>(Right) << QPoint(317, 50);
    QTest::newRow("top") << static_cast<int>(Top) << QPoint(40, -5);
    QTest::newRow("vertical-center") << static_cast<int>(VerticalCenter) << QPoint(40, 115);
    QTest::newRow("bottom") << static_cast<int>(Bottom) << QPoint(40, 235);
}

void SessionTests::alignmentUsesRenderedFootprint()
{
    QFETCH(int, alignment);
    QFETCH(QPoint, expected);
    Designer::DocumentSession session;
    auto document = simpleDocument();
    document.width = 400;
    document.height = 300;
    document.elements[0].x = 40;
    document.elements[0].y = 50;
    session.reset(document);
    QVERIFY(session.alignElements({0}, static_cast<Designer::ElementAlignment>(alignment),
                                   Designer::AlignmentReference::Label, {QRect(43, 55, 80, 60)}));
    const auto& aligned = session.document().elements[0];
    QCOMPARE(QPoint(aligned.x, aligned.y), expected);
    QCOMPARE(session.undoStack()->count(), 1);
    session.undoStack()->undo();
    QCOMPARE(session.document().elements[0].x, 40);
    QCOMPARE(session.document().elements[0].y, 50);
}

void SessionTests::selectionAlignmentAndDistribution()
{
    Designer::DocumentSession session;
    Designer::DesignDocument document;
    for (const auto position : {QPoint(20, 30), QPoint(80, 70), QPoint(220, 230)})
        document.elements.append(Designer::makeElement(Designer::ElementType::Box, position));
    const QList<QRect> bounds{QRect(20, 30, 40, 20), QRect(80, 70, 60, 40), QRect(220, 230, 20, 20)};
    for (qsizetype index = 0; index < document.elements.size(); ++index) {
        document.elements[index].width = bounds[index].width();
        document.elements[index].height = bounds[index].height();
    }
    session.reset(document);
    QVERIFY(session.alignElements({0, 1, 2}, Designer::ElementAlignment::Right,
                                   Designer::AlignmentReference::Selection, bounds));
    QCOMPARE(session.document().elements[0].x, 200);
    QCOMPARE(session.document().elements[1].x, 180);
    QCOMPARE(session.document().elements[2].x, 220);
    QCOMPARE(session.undoStack()->count(), 1);
    session.undoStack()->undo();
    QVERIFY(session.distributeElements({2, 0, 1}, Qt::Horizontal, bounds));
    QCOMPARE(session.document().elements[0].x, 20);
    QCOMPARE(session.document().elements[1].x, 110);
    QCOMPARE(session.document().elements[2].x, 220);
    QCOMPARE(session.undoStack()->count(), 1);
    session.undoStack()->undo();
    QVERIFY(session.distributeElements({2, 0, 1}, Qt::Vertical, bounds));
    QCOMPARE(session.document().elements[0].y, 30);
    QCOMPARE(session.document().elements[1].y, 120);
    QCOMPARE(session.document().elements[2].y, 230);
    session.undoStack()->undo();
    QVERIFY(!session.distributeElements({0, 1}, Qt::Horizontal, bounds));
    QVERIFY(!session.alignElements({0}, Designer::ElementAlignment::Left,
                                    Designer::AlignmentReference::Selection, bounds));
}

void SessionTests::groupOperationsRejectSourceModeAndExcessCopies()
{
    Designer::DocumentSession session;
    auto document = simpleDocument();
    document.visualEditable = false;
    document.originalZpl = QStringLiteral("^XA^FXUnchanged^FS^XZ");
    session.reset(document);
    QVERIFY(!session.moveElements({0}, {1, 1}));
    QVERIFY(!session.deleteElements({0}));
    QVERIFY(session.duplicateElements({0}).isEmpty());
    QVERIFY(!session.alignElements({0}, Designer::ElementAlignment::Left, Designer::AlignmentReference::Label));
    QVERIFY(!session.distributeElements({0}, Qt::Horizontal));
    QCOMPARE(session.document().originalZpl, document.originalZpl);
    QVERIFY(!session.undoStack()->canUndo());
    document.visualEditable = true;
    while (document.elements.size() < 199)
        document.elements.append(Designer::makeElement(Designer::ElementType::Box));
    session.reset(document);
    QVERIFY(session.duplicateElements({0, 1}).isEmpty());
    QCOMPARE(session.document().elements.size(), 199);
    QVERIFY(!session.undoStack()->canUndo());
}

void SessionTests::clippedShapesUseFullGeometryAndKeepNegativeOtherAxis()
{
    Designer::DocumentSession session;
    Designer::DesignDocument document;
    document.width = 400;
    document.height = 300;
    auto box = Designer::makeElement(Designer::ElementType::Box, {350, -20});
    box.width = 160;
    box.height = 80;
    document.elements.append(box);
    session.reset(document);
    QVERIFY(session.alignElements({0}, Designer::ElementAlignment::HorizontalCenter,
                                   Designer::AlignmentReference::Label, {QRect(350, 0, 50, 60)}));
    QCOMPARE(session.document().elements[0].x, 120);
    QCOMPARE(session.document().elements[0].y, -20);
    QCOMPARE(session.document().elements[0].width, 160);
}

void SessionTests::filledExportPreservesTemplateAndSession()
{
    Designer::DesignDocument document;
    auto dm = Designer::makeElement(Designer::ElementType::DataMatrix);
    QVERIFY(dm.gs1);
    dm.text = QStringLiteral("(01){{gtin}}(21)SN-[[serial]]");
    document.elements = {dm};
    Designer::DocumentSession session;
    session.reset(document);
    QTemporaryDir dir;
    const auto templatePath = dir.filePath(QStringLiteral("template.zpl"));
    const auto filledPath = dir.filePath(QStringLiteral("filled.zpl"));
    QVERIFY(session.save(templatePath));
    const auto original = Designer::toZpl(session.document());
    QVERIFY(!session.saveResolved(filledPath));
    QVERIFY(!QFile::exists(filledPath));
    session.setPreviewValues({{QStringLiteral("gtin"), QStringLiteral("09506000134352")},
                              {QStringLiteral("serial"), QStringLiteral("A123")}});
    QVERIFY(!session.modified());
    QVERIFY(!session.undoStack()->canUndo());
    auto changed = session.document(); changed.elements.first().x = 72;
    session.commit(changed, QStringLiteral("Move"));
    session.undoStack()->undo();
    QCOMPARE(session.document().previewValues.value(QStringLiteral("serial")), QStringLiteral("A123"));
    QVERIFY(session.saveResolved(filledPath));
    QVERIFY(!QFile::exists(filledPath + QStringLiteral(".layout.json")));
    QVERIFY(!QFile::exists(Designer::DocumentSession::layoutStoragePath(filledPath)));
    QVERIFY(!session.modified());
    QCOMPARE(session.filePath(), templatePath);
    QCOMPARE(Designer::toZpl(session.document()), original);
    QFile filled(filledPath); QVERIFY(filled.open(QIODevice::ReadOnly));
    const auto filledBytes = filled.readAll(); filled.close();
    QVERIFY(!filledBytes.contains("{{")); QVERIFY(!filledBytes.contains("[["));
    auto imported = Designer::fromZpl(QString::fromUtf8(filledBytes));
    QVERIFY(imported); QVERIFY(imported->visualEditable);
    QCOMPARE(imported->elements.first().text, QStringLiteral("(01)09506000134352(21)SN-A123"));
    QVERIFY(!session.saveResolved(templatePath));
    QFile source(templatePath); QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(source.readAll()), original); source.close();
    session.setPreviewValues({{QStringLiteral("gtin"), QStringLiteral("bad")}});
    QVERIFY(!session.saveResolved(filledPath));
    QVERIFY(filled.open(QIODevice::ReadOnly)); QCOMPARE(filled.readAll(), filledBytes); filled.close();
    QVERIFY(session.open(templatePath));
    QVERIFY(session.document().visualEditable);
    QVERIFY(session.document().previewValues.isEmpty());
    QCOMPARE(session.document().elements.first().text, dm.text);
}

void SessionTests::layoutSettingsHaveIndependentUndo()
{
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    const auto source = Designer::toZpl(session.document());
    QSignalSpy documentChanged(&session, &Designer::DocumentSession::documentChanged);
    QSignalSpy layoutChanged(&session, &Designer::DocumentSession::layoutSettingsChanged);
    const auto settings = customLayout();
    session.setLayoutSettings(settings, QStringLiteral("Layout"));
    QCOMPARE(session.layoutSettings(), settings);
    QCOMPARE(documentChanged.size(), 0);
    QCOMPARE(layoutChanged.size(), 1);
    QCOMPARE(Designer::toZpl(session.document()), source);
    QVERIFY(session.modified());
    session.setLayoutSettings(settings);
    QCOMPARE(session.undoStack()->count(), 1);

    auto moved = session.document();
    moved.elements[0].x = 60;
    session.commit(moved, QStringLiteral("Move"));
    session.undoStack()->undo();
    QCOMPARE(session.layoutSettings(), settings);
    QCOMPARE(layoutChanged.size(), 1);
    documentChanged.clear();
    session.undoStack()->undo();
    QCOMPARE(session.layoutSettings(), Designer::CanvasLayoutSettings{});
    QCOMPARE(documentChanged.size(), 0);
    QVERIFY(!session.modified());
    session.undoStack()->redo();
    QCOMPARE(session.layoutSettings(), settings);
    QCOMPARE(documentChanged.size(), 0);
    session.reset(simpleDocument());
    QCOMPARE(session.layoutSettings(), Designer::CanvasLayoutSettings{});
    QVERIFY(session.layoutWarning().isEmpty());
    QVERIFY(!session.modified());
}

void SessionTests::layoutGuidesUseOneCanonicalRepresentation()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("guides.zpl"));
    const auto sidecar = Designer::DocumentSession::layoutStoragePath(path);
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    auto settings = session.layoutSettings();
    settings.horizontalGuides = {100, 20, 20};
    settings.verticalGuides = {90, 0, 90};
    session.setLayoutSettings(settings);
    QCOMPARE(session.layoutSettings().horizontalGuides, QList<int>({20, 100}));
    QCOMPARE(session.layoutSettings().verticalGuides, QList<int>({0, 90}));
    QSignalSpy changed(&session, &Designer::DocumentSession::layoutSettingsChanged);
    settings.horizontalGuides = {20, 100, 100};
    session.setLayoutSettings(settings);
    QCOMPARE(changed.size(), 0);
    QCOMPARE(session.undoStack()->count(), 1);
    session.undoStack()->undo();
    QVERIFY(session.layoutSettings().horizontalGuides.isEmpty());
    session.undoStack()->redo();
    QCOMPARE(session.layoutSettings().horizontalGuides, QList<int>({20, 100}));
    QVERIFY(session.save(path));

    auto metadata = QJsonDocument::fromJson(readBytes(sidecar)).object();
    auto layout = metadata.value(QStringLiteral("layout")).toObject();
    layout.insert(QStringLiteral("horizontalGuides"), QJsonArray{100, 20, 20});
    layout.insert(QStringLiteral("verticalGuides"), QJsonArray{90, 0, 90});
    metadata.insert(QStringLiteral("layout"), layout);
    QVERIFY(writeBytes(sidecar, QJsonDocument(metadata).toJson()));
    QVERIFY(session.open(path));
    QCOMPARE(session.layoutSettings().horizontalGuides, QList<int>({20, 100}));
    QCOMPARE(session.layoutSettings().verticalGuides, QList<int>({0, 90}));
    QVERIFY(session.layoutWarning().isEmpty());
    QVERIFY(!session.modified());
}

void SessionTests::layoutRoundTripAndSaveAs()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto first = directory.filePath(QStringLiteral("first.zpl"));
    const auto second = directory.filePath(QStringLiteral("second.zpl"));
    Designer::DocumentSession session;
    auto document = simpleDocument();
    document.dpi = 600;
    session.reset(document);
    session.setLayoutSettings(customLayout());
    session.setPreviewValues({{QStringLiteral("private"), QStringLiteral("DO_NOT_SAVE_VALUE")}});
    QVERIFY(session.save(first));
    const auto firstMetadata = readBytes(Designer::DocumentSession::layoutStoragePath(first));
    QVERIFY(!firstMetadata.isEmpty());
    QVERIFY(!QFile::exists(first + QStringLiteral(".layout.json")));
    QVERIFY(Designer::DocumentSession::layoutStoragePath(first).startsWith(
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u'/'));
    QVERIFY(!firstMetadata.contains("DO_NOT_SAVE_VALUE"));
    QVERIFY(!firstMetadata.contains("previewValues"));
    const auto json = QJsonDocument::fromJson(firstMetadata).object();
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 1);
    QCOMPARE(json.value(QStringLiteral("sha256")).toString().toLatin1(),
             QCryptographicHash::hash(readBytes(first), QCryptographicHash::Sha256).toHex());

    auto modifiedSettings = customLayout();
    modifiedSettings.gridStep = 31;
    session.setLayoutSettings(modifiedSettings);
    QVERIFY(session.save(second));
    QCOMPARE(session.filePath(), second);
    QVERIFY(!session.modified());
    QCOMPARE(readBytes(Designer::DocumentSession::layoutStoragePath(first)), firstMetadata);
    Designer::DocumentSession reopened;
    QSignalSpy layoutChanged(&reopened, &Designer::DocumentSession::layoutSettingsChanged);
    QVERIFY(reopened.open(second));
    QCOMPARE(reopened.layoutSettings(), modifiedSettings);
    QCOMPARE(reopened.document().dpi, 600);
    QCOMPARE(layoutChanged.size(), 1);
    QVERIFY(reopened.layoutWarning().isEmpty());
    QVERIFY(reopened.document().previewValues.isEmpty());
    QVERIFY(!reopened.modified());
    QVERIFY(!reopened.undoStack()->canUndo());
    QVERIFY(reopened.open(first));
    QCOMPARE(reopened.layoutSettings(), customLayout());
}

void SessionTests::legacyLayoutMigratesWithoutChangingSidecar()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("legacy.zpl"));
    const auto localPath = Designer::DocumentSession::layoutStoragePath(path);
    const auto legacyPath = path + QStringLiteral(".layout.json");
    Designer::DocumentSession session;
    auto document = simpleDocument();
    document.dpi = 300;
    session.reset(document);
    session.setLayoutSettings(customLayout());
    QVERIFY(session.save(path));
    const auto source = readBytes(path);
    const auto oldMetadata = readBytes(localPath);
    QVERIFY(!oldMetadata.isEmpty());
    QVERIFY(writeBytes(legacyPath, oldMetadata));
    QVERIFY(QFile::remove(localPath));

    Designer::DocumentSession reopened;
    QVERIFY(reopened.open(path));
    QCOMPARE(reopened.layoutSettings(), customLayout());
    QCOMPARE(reopened.document().dpi, 300);
    QVERIFY(reopened.layoutWarning().isEmpty());
    QVERIFY(!QFile::exists(localPath));
    auto settings = customLayout();
    settings.gridStep = 31;
    reopened.setLayoutSettings(settings);
    QVERIFY(reopened.save(path));
    QCOMPARE(readBytes(path), source);
    QCOMPARE(readBytes(legacyPath), oldMetadata);
    QVERIFY(QFile::exists(localPath));
    QVERIFY(reopened.open(path));
    QCOMPARE(reopened.layoutSettings(), settings);
    QCOMPARE(reopened.document().dpi, 300);
}

void SessionTests::localLayoutDoesNotRequireSidecarAccess()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("label.zpl"));
    // A legacy metadata path that cannot be written must not prevent saving.
    const auto sidecar = path + QStringLiteral(".layout.json");
    QVERIFY(QDir().mkdir(sidecar));
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    session.setLayoutSettings(customLayout());
    QVERIFY(session.save(path));
    QVERIFY(session.open(path));
    QCOMPARE(session.layoutSettings(), customLayout());
    QVERIFY(session.layoutWarning().isEmpty());
    QVERIFY(QFileInfo(sidecar).isDir());
#ifdef Q_OS_WIN
    QCOMPARE(Designer::DocumentSession::layoutStoragePath(path.toUpper()),
             Designer::DocumentSession::layoutStoragePath(path));
#endif
}

void SessionTests::missingLayoutRestoresDefaults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("external.zpl"));
    const auto source = Designer::toZpl(simpleDocument()).toUtf8();
    QVERIFY(writeBytes(path, source));
    Designer::DocumentSession session;
    auto document = simpleDocument();
    document.dpi = 300;
    session.reset(document);
    session.setLayoutSettings(customLayout());
    QVERIFY(session.open(path));
    QCOMPARE(session.layoutSettings(), Designer::CanvasLayoutSettings{});
    QCOMPARE(session.document().dpi, 203);
    QVERIFY(session.layoutWarning().isEmpty());
    QCOMPARE(Designer::toZpl(session.document()).toUtf8(), source);
}

void SessionTests::corruptOrMismatchedLayoutKeepsZplUsable()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("label.zpl"));
    const auto sidecar = Designer::DocumentSession::layoutStoragePath(path);
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    session.setLayoutSettings(customLayout());
    QVERIFY(session.save(path));
    const auto metadata = readBytes(sidecar);
    const auto source = readBytes(path);
    for (const auto& corrupt : {QByteArray("{broken"), QByteArray(64 * 1024 + 1, ' ')}) {
        QVERIFY(writeBytes(sidecar, corrupt));
        QVERIFY(session.open(path));
        QCOMPARE(session.layoutSettings(), Designer::CanvasLayoutSettings{});
        QCOMPARE(session.document().dpi, 203);
        QVERIFY(!session.layoutWarning().isEmpty());
        QCOMPARE(Designer::toZpl(session.document()).toUtf8(), source);
    }
    QVERIFY(writeBytes(sidecar, metadata));
    auto different = simpleDocument();
    different.elements[0].text = QStringLiteral("EXTERNAL CHANGE");
    const auto changedSource = Designer::toZpl(different).toUtf8();
    QVERIFY(writeBytes(path, changedSource));
    QVERIFY(session.open(path));
    QCOMPARE(session.layoutSettings(), Designer::CanvasLayoutSettings{});
    QVERIFY(!session.layoutWarning().isEmpty());
    QCOMPARE(Designer::toZpl(session.document()).toUtf8(), changedSource);
    QVERIFY(session.save(path));
    QVERIFY(session.layoutWarning().isEmpty());
    QVERIFY(session.open(path));
    QVERIFY(session.layoutWarning().isEmpty());

    QVERIFY(QFile::remove(sidecar));
    QVERIFY(QDir().mkdir(sidecar));
    QVERIFY(session.open(path));
    QVERIFY(!session.layoutWarning().isEmpty());
    QCOMPARE(session.layoutSettings(), Designer::CanvasLayoutSettings{});
}

void SessionTests::layoutValidationRejectsInvalidMetadata()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("label.zpl"));
    const auto sidecar = Designer::DocumentSession::layoutStoragePath(path);
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    QVERIFY(session.save(path));
    const auto original = QJsonDocument::fromJson(readBytes(sidecar)).object();
    QList<QJsonObject> invalid;
    for (const auto& value : {QJsonValue(0), QJsonValue(4097), QJsonValue(1.5), QJsonValue("8")}) {
        auto object = original;
        auto layout = object.value(QStringLiteral("layout")).toObject();
        layout.insert(QStringLiteral("gridStep"), value);
        object.insert(QStringLiteral("layout"), layout);
        invalid.append(object);
    }
    const QList<QPair<QString, QJsonValue>> fields{
        {QStringLiteral("margin"), -1}, {QStringLiteral("margin"), 2049},
        {QStringLiteral("unit"), QStringLiteral("inch")},
        {QStringLiteral("gridVisible"), 1},
        {QStringLiteral("horizontalGuides"), QJsonArray{-1}},
        {QStringLiteral("verticalGuides"), QJsonArray{4097}},
        {QStringLiteral("verticalGuides"), QJsonArray{1.5}}
    };
    for (const auto& [key, value] : fields) {
        auto object = original;
        auto layout = object.value(QStringLiteral("layout")).toObject();
        layout.insert(key, value);
        object.insert(QStringLiteral("layout"), layout);
        invalid.append(object);
    }
    auto tooMany = original;
    auto layout = tooMany.value(QStringLiteral("layout")).toObject();
    QJsonArray guides;
    for (int index = 0; index < 129; ++index) guides.append(index);
    layout.insert(QStringLiteral("horizontalGuides"), guides);
    tooMany.insert(QStringLiteral("layout"), layout);
    invalid.append(tooMany);
    auto badDpi = original;
    badDpi.insert(QStringLiteral("dpi"), 406);
    invalid.append(badDpi);
    auto badVersion = original;
    badVersion.insert(QStringLiteral("version"), 2);
    invalid.append(badVersion);
    for (const auto& object : invalid) {
        QVERIFY(writeBytes(sidecar, QJsonDocument(object).toJson()));
        QVERIFY(session.open(path));
        QVERIFY(!session.layoutWarning().isEmpty());
        QCOMPARE(session.layoutSettings(), Designer::CanvasLayoutSettings{});
        QCOMPARE(session.document().dpi, 203);
    }
}

void SessionTests::failedLayoutSaveKeepsZplAndDirtyState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("label.zpl"));
    const auto sidecar = Designer::DocumentSession::layoutStoragePath(path);
    Designer::DocumentSession session;
    session.reset(simpleDocument());
    QVERIFY(session.save(path));
    const auto source = readBytes(path);
    QVERIFY(QFile::remove(sidecar));
    QVERIFY(QDir().mkdir(sidecar));
    auto changed = session.document();
    changed.elements[0].text = QStringLiteral("UNSAVED CHANGE");
    session.commit(changed, QStringLiteral("Edit"));
    session.setLayoutSettings(customLayout());
    const auto result = session.save(path);
    QVERIFY(!result);
    QVERIFY(result.error().contains(QStringLiteral("ZPL не изменён")));
    QVERIFY(session.modified());
    QCOMPARE(readBytes(path), source);
    QCOMPARE(session.layoutSettings(), customLayout());
    QCOMPARE(session.filePath(), path);
}

void SessionTests::layoutEditsPreserveUnsupportedSourceBytes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("unsupported.zpl"));
    const QByteArray source("^XA\r\n^PW200\r\n^LL100\r\n^ZZunknown\r\n^FO5,5^GB20,20,2^FS\r\n^XZ\r\n");
    QVERIFY(writeBytes(path, source));
    Designer::DocumentSession session;
    QVERIFY(session.open(path));
    QVERIFY(!session.document().visualEditable);
    session.setLayoutSettings(customLayout());
    QVERIFY(session.save(path));
    QCOMPARE(readBytes(path), source);
    QVERIFY(session.open(path));
    QCOMPARE(session.layoutSettings(), customLayout());
    QCOMPARE(Designer::toZpl(session.document()).toUtf8(), source);
    QVERIFY(!session.document().visualEditable);
}

QTEST_MAIN(SessionTests)
#include "test_session.moc"
