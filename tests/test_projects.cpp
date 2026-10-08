#include "document_session.hpp"
#include "project_codec.hpp"

#include <QDir>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
DesignDocument editableDocument()
{
    DesignDocument document;
    auto text = makeElement(ElementType::Text, {32, 40});
    text.text = u"Part {{part}}"_s;
    text.name = u"Название товара"_s;
    document.elements.append(text);
    return document;
}

bool writeBytes(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace

class ProjectTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void portableProjectPreservesColorAndEditorState();
    void tableAndBarcodesRemainEditableInProject();
    void editableImportPreservesSourceTokens();
    void sourceOnlyProjectIsLossless();
    void invalidInputDoesNotReplaceCurrentDocument();
    void pendingInvalidSourceCanBeSavedAndReopened();
    void projectExportLeavesProjectAndHistoryUnchanged();
    void recoveryPreservesDraftAndIsExclusive();
    void corruptRecoveryDoesNotReplaceCurrentDocument();
    void pendingSourceTriggersDebouncedRecovery();
    void lockedGroupMutationsAreAtomic();
};

void ProjectTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(u"QtZplTests"_s);
    QCoreApplication::setApplicationName(u"projects-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

void ProjectTests::cleanupTestCase()
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

void ProjectTests::portableProjectPreservesColorAndEditorState()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    DocumentSession session;
    auto document = editableDocument();
    document.dpi = 300;
    auto image = makeElement(ElementType::Image, {17, 29});
    image.image = QImage(3, 2, QImage::Format_ARGB32);
    image.image.fill(QColor(230, 40, 80, 120));
    image.image.setPixelColor(1, 1, QColor(10, 170, 240, 255));
    image.threshold = 97;
    image.dither = true;
    image.reverse = true;
    image.imageKeepAspectRatio = false;
    image.locked = true;
    image.editorVisible = false;
    image.printable = false;
    document.elements[0].groupId = QUuid::createUuid();
    image.groupId = document.elements[0].groupId;
    document.elements.append(image);
    session.reset(document);
    auto layout = session.layoutSettings();
    layout.unit = CanvasUnit::Dots;
    layout.horizontalGuides = {19, 90};
    layout.verticalGuides = {7};
    layout.gridStep = 13;
    session.setLayoutSettings(layout);
    session.setPreviewValues({{u"part"_s, u"PRIVATE-VALUE"_s}});
    const auto path = directory.filePath(u"portable.qtzpl"_s);
    const auto saved = session.save(path);
    QVERIFY2(saved.has_value(), saved ? "" : qPrintable(saved.error()));
    QVERIFY(!session.modified());
    QVERIFY(!readBytes(path).contains("PRIVATE-VALUE"));
    QVERIFY(!QFileInfo::exists(DocumentSession::layoutStoragePath(path)));
    DocumentSession reopened;
    const auto opened = reopened.open(path);
    QVERIFY2(opened.has_value(), opened ? "" : qPrintable(opened.error()));
    QCOMPARE(reopened.document().dpi, 300);
    QCOMPARE(reopened.layoutSettings(), layout);
    QCOMPARE(reopened.document().elements[0].id, document.elements[0].id);
    QCOMPARE(reopened.document().elements[1].groupId, document.elements[0].groupId);
    const auto& restored = reopened.document().elements[1];
    QCOMPARE(restored.image.pixelColor(0, 0), image.image.pixelColor(0, 0));
    QCOMPARE(restored.image.pixelColor(1, 1), image.image.pixelColor(1, 1));
    QCOMPARE(restored.threshold, 97);
    QVERIFY(restored.dither && restored.reverse && restored.locked);
    QVERIFY(!restored.editorVisible && !restored.printable && !restored.imageKeepAspectRatio);
    QVERIFY(reopened.document().previewValues.isEmpty());
}

void ProjectTests::tableAndBarcodesRemainEditableInProject()
{
    auto document = editableDocument();
    auto table = makeElement(ElementType::Table, {30, 140});
    table.tableCells[0] = u"Name {{part}}"_s;
    table.tableBorders = false;
    document.elements.append(table);
    auto ean = makeElement(ElementType::Ean13);
    // Incomplete user input belongs in a project even before it can print.
    ean.text = u"123"_s;
    document.elements.append(ean);
    auto qr = makeElement(ElementType::QrCode);
    qr.qrErrorCorrection = u'H';
    document.elements.append(qr);
    auto pdf = makeElement(ElementType::Pdf417);
    pdf.pdf417Columns = 4;
    pdf.pdf417Security = 5;
    document.elements.append(pdf);
    const auto encoded = encodeProject({document, {}, {}});
    QVERIFY2(encoded.has_value(), encoded ? "" : qPrintable(encoded.error()));
    const auto restored = decodeProject(*encoded);
    QVERIFY(restored.has_value());
    QCOMPARE(restored->document.elements[1].type, ElementType::Table);
    QCOMPARE(restored->document.elements[1].tableCells, table.tableCells);
    QCOMPARE(restored->document.elements[1].tableColumnWidths, table.tableColumnWidths);
    QVERIFY(!restored->document.elements[1].tableBorders);
    QCOMPARE(restored->document.elements[2].text, u"123"_s);
    QCOMPARE(restored->document.elements[3].qrErrorCorrection, QChar(u'H'));
    QCOMPARE(restored->document.elements[4].pdf417Columns, 4);
    QCOMPARE(restored->document.elements[4].pdf417Security, 5);
}

void ProjectTests::editableImportPreservesSourceTokens()
{
    const QString source = u"^XA\r\n^PW812^LL406^FXLeave this spelling intact^FS\r\n^FO24,32^A0N,30,20^FDPart 42^FS\r\n^PQ1^XZ\r\n"_s;
    const auto parsed = fromZpl(source);
    QVERIFY(parsed.has_value());
    QVERIFY(parsed->visualEditable);
    const auto encoded = encodeProject({*parsed, {}, {}});
    QVERIFY2(encoded.has_value(), encoded ? "" : qPrintable(encoded.error()));
    const auto reopened = decodeProject(*encoded);
    QVERIFY(reopened.has_value());
    QCOMPARE(toZpl(reopened->document), source);
    QCOMPARE(reopened->document.sourceOrder, parsed->sourceOrder);
    QCOMPARE(reopened->document.elements[0].sourceZpl, parsed->elements[0].sourceZpl);
    auto edited = reopened->document;
    edited.originalZpl.clear();
    edited.elements[0].x += 10;
    QVERIFY(toZpl(edited).contains(u"^FXLeave this spelling intact^FS"));
    QVERIFY(toZpl(edited).contains(u"^PQ1"));
}

void ProjectTests::sourceOnlyProjectIsLossless()
{
    const auto label = toZpl(editableDocument());
    const auto source = label + u"\r\n"_s + label;
    const auto parsed = fromZpl(source);
    QVERIFY(parsed.has_value());
    QVERIFY(!parsed->visualEditable);
    const auto bytes = encodeProject({*parsed, {}, {}});
    QVERIFY(bytes.has_value());
    const auto decoded = decodeProject(*bytes);
    QVERIFY(decoded.has_value());
    QVERIFY(!decoded->document.visualEditable);
    QCOMPARE(toZpl(decoded->document), source);
}

void ProjectTests::invalidInputDoesNotReplaceCurrentDocument()
{
    QTemporaryDir directory;
    DocumentSession session;
    session.reset(editableDocument());
    session.setPendingSource(u"unfinished source"_s);
    auto encoded = encodeProject({editableDocument(), {}, {}});
    QVERIFY(encoded.has_value());
    auto root = QJsonDocument::fromJson(*encoded).object();
    root[u"version"_s] = 2;
    const auto path = directory.filePath(u"unsupported.qtzpl"_s);
    QVERIFY(writeBytes(path, QJsonDocument(root).toJson()));
    const auto unsupported = session.open(path);
    QVERIFY(!unsupported.has_value());
    QVERIFY(unsupported.error().contains(u"Версия"));
    QCOMPARE(session.document().elements[0].text, editableDocument().elements[0].text);
    QCOMPARE(session.pendingSource(), u"unfinished source"_s);
    QVERIFY(session.modified());
    root[u"version"_s] = 1;
    root[u"width"_s] = 60000;
    QVERIFY(writeBytes(path, QJsonDocument(root).toJson()));
    QVERIFY(!session.open(path).has_value());
    QVERIFY(writeBytes(path, QByteArrayLiteral("{bad-json")));
    QVERIFY(!session.open(path).has_value());
    QCOMPARE(session.pendingSource(), u"unfinished source"_s);
    session.discardRecovery();
}

void ProjectTests::pendingInvalidSourceCanBeSavedAndReopened()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(u"unfinished.qtzpl"_s);
    DocumentSession session;
    session.reset(editableDocument());
    const auto applied = toZpl(session.document());
    session.setPendingSource(u"^XA\n^FO123,"_s);
    QVERIFY(session.modified());
    QVERIFY(!session.save(directory.filePath(u"unfinished.zpl"_s)).has_value());
    QVERIFY(session.save(path).has_value());
    QVERIFY(!session.modified());
    QVERIFY(session.hasPendingSource());
    DocumentSession opened;
    QVERIFY(opened.open(path).has_value());
    QCOMPARE(toZpl(opened.document()), applied);
    QVERIFY(opened.hasPendingSource());
    QCOMPARE(opened.pendingSource(), u"^XA\n^FO123,"_s);
    QVERIFY(!opened.modified());
    opened.setPendingSource(u"^XA\n^FO124,"_s);
    QVERIFY(opened.modified());
    opened.setPendingSource(u"^XA\n^FO123,"_s);
    QVERIFY(!opened.modified());
}

void ProjectTests::projectExportLeavesProjectAndHistoryUnchanged()
{
    QTemporaryDir directory;
    DocumentSession session;
    session.reset(editableDocument());
    const auto project = directory.filePath(u"label.qtzpl"_s);
    QVERIFY(session.save(project).has_value());
    auto next = session.document();
    next.elements[0].x = 70;
    session.commit(next, u"Move"_s);
    const int historyIndex = session.undoStack()->index();
    const auto before = readBytes(project);
    const auto output = directory.filePath(u"label.zpl"_s);
    QVERIFY(session.exportZpl(output).has_value());
    QCOMPARE(session.filePath(), project);
    QCOMPARE(session.undoStack()->index(), historyIndex);
    QVERIFY(session.modified());
    QCOMPARE(readBytes(project), before);
    QCOMPARE(QString::fromUtf8(readBytes(output)), toZpl(next));
    QVERIFY(!session.exportZpl(project).has_value());
    session.discardRecovery();
}

void ProjectTests::recoveryPreservesDraftAndIsExclusive()
{
    QTemporaryDir directory;
    QString recoveryPath;
    const auto original = directory.filePath(u"original.qtzpl"_s);
    {
        DocumentSession crashed;
        crashed.reset(editableDocument());
        QVERIFY(crashed.save(original).has_value());
        auto layout = crashed.layoutSettings();
        layout.verticalGuides = {29};
        crashed.setLayoutSettings(layout);
        crashed.setPendingSource(u"unfinished ^FD"_s);
        QVERIFY(crashed.flushRecovery().has_value());
        recoveryPath = crashed.recoveryFilePath();
        QVERIFY(QFileInfo::exists(recoveryPath));
        QVERIFY(DocumentSession::availableRecoveries().isEmpty());
        QVERIFY(!DocumentSession::discardRecovery(recoveryPath).has_value());
        DocumentSession otherWindow;
        otherWindow.reset(editableDocument());
        otherWindow.setPendingSource(u"another draft"_s);
        QVERIFY(otherWindow.flushRecovery().has_value());
        QVERIFY(otherWindow.recoveryFilePath() != recoveryPath);
        otherWindow.discardRecovery();
        QVERIFY(QFileInfo::exists(recoveryPath));
    }
    const auto entries = DocumentSession::availableRecoveries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries[0].path, recoveryPath);
    QCOMPARE(entries[0].originalPath, original);
    QVERIFY(entries[0].error.isEmpty());
    DocumentSession restored;
    QVERIFY(restored.recover(recoveryPath).has_value());
    QVERIFY(restored.modified());
    QCOMPARE(restored.pendingSource(), u"unfinished ^FD"_s);
    QCOMPARE(restored.layoutSettings().verticalGuides, QList<int>{29});
    QCOMPARE(restored.filePath(), original);
    QVERIFY(DocumentSession::availableRecoveries().isEmpty());
    QVERIFY(restored.save(original).has_value());
    QVERIFY(!QFileInfo::exists(recoveryPath));
    QVERIFY(DocumentSession::availableRecoveries().isEmpty());
}

void ProjectTests::corruptRecoveryDoesNotReplaceCurrentDocument()
{
    const auto directory = DocumentSession::recoveryStorageDirectory();
    QVERIFY(QDir().mkpath(directory));
    const auto path = QDir(directory).filePath(u"broken.qtzpl.recovery"_s);
    QVERIFY(writeBytes(path, QByteArrayLiteral("{broken")));
    const auto entries = DocumentSession::availableRecoveries();
    QCOMPARE(entries.size(), 1);
    QVERIFY(!entries[0].error.isEmpty());
    DocumentSession session;
    session.reset(editableDocument());
    session.setPendingSource(u"keep this"_s);
    QVERIFY(!session.recover(path).has_value());
    QCOMPARE(session.pendingSource(), u"keep this"_s);
    QCOMPARE(session.document().elements.size(), 1);
    QVERIFY(DocumentSession::discardRecovery(path).has_value());
    QVERIFY(DocumentSession::availableRecoveries().isEmpty());
    session.discardRecovery();
}

void ProjectTests::pendingSourceTriggersDebouncedRecovery()
{
    DocumentSession session;
    session.reset(editableDocument());
    session.setPendingSource(u"unfinished"_s);
    QTRY_VERIFY_WITH_TIMEOUT(!session.recoveryFilePath().isEmpty(), 4000);
    const auto path = session.recoveryFilePath();
    QVERIFY(QFileInfo::exists(path));
    session.clearPendingSource();
    QVERIFY(!session.modified());
    QVERIFY(!QFileInfo::exists(path));
}

void ProjectTests::lockedGroupMutationsAreAtomic()
{
    auto document = editableDocument();
    document.elements.append(makeElement(ElementType::Box, {150, 120}));
    const auto group = QUuid::createUuid();
    document.elements[0].groupId = group;
    document.elements[1].groupId = group;
    document.elements[1].locked = true;
    DocumentSession session;
    session.reset(document);
    QVERIFY(!session.moveElements({0}, {1, 1}));
    QVERIFY(!session.deleteElements({0}));
    QVERIFY(!session.groupElements({0, 1}));
    QVERIFY(!session.ungroupElements({0}));
    QVERIFY(!session.alignElements({0}, ElementAlignment::Left, AlignmentReference::Label));
    QVERIFY(!session.distributeElements({0, 1}, Qt::Horizontal));
    QVERIFY(!session.modified());
    QCOMPARE(session.document().elements[0].x, document.elements[0].x);
    QCOMPARE(session.document().elements.size(), 2);
}

QTEST_MAIN(ProjectTests)
#include "test_projects.moc"
