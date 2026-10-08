#include "label_printing.hpp"
#include "print_dialog.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QLineEdit>
#include <QMessageBox>
#include <QPageSize>
#include <QPrinter>
#include <QPushButton>
#include <QSpinBox>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QStandardPaths>
#include <QUuid>
#include <QtTest>

#include <chrono>
#include <cmath>
#include <future>
#include <memory>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
DesignDocument label()
{
    DesignDocument document;
    document.width = 600;
    document.height = 1200;
    document.dpi = 300;
    auto text = makeElement(ElementType::Text, {24, 30});
    text.text = u"Тест этикетки"_s;
    document.elements = {text};
    return document;
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
} // namespace

class PrintingTests final : public QObject {
    Q_OBJECT
private slots:
    void physicalGeometryDoesNotDependOnScreenDpi();
    void rawPreparationPreservesSourceAndQuantity();
    void templateValuesAreResolvedBeforeEitherMode();
    void imageBecomesOneLosslessGraphicField();
    void pngPreservesPixelsAndConfiguredDpi();
    void pngRejectsInvalidJobsAndDriverSubmission();
    void pngSizeLimitCountsAllRepeats();
    void invalidAndMultipleLabelJobsAreRejected();
    void imagePrintsToExactSizePdf();
    void imageFitsOrdinaryPaperAtActualSize();
    void tcpStreamsExactBytesAndRepeats();
    void pngRepeatsUseSeparateTcpConnections();
    void tcpConnectionFailureIsReported();
    void cancelledDialogDoesNotSaveOrSubmit();
    void closedBusyDialogCompletesInBackground();
};

void PrintingTests::physicalGeometryDoesNotDependOnScreenDpi()
{
    const auto prepared = prepareLabelPrint(label(), LabelPrintMode::Image);
    QVERIFY2(prepared.has_value(), prepared ? "" : qPrintable(prepared.error()));
    QCOMPARE(prepared->pageDots, QSize(600, 1200));
    QCOMPARE(printPageMillimeters(*prepared), QSizeF(50.8, 101.6));
    QCOMPARE(printImageTargetRect(*prepared, 300, 300), QRectF(0, 0, 600, 1200));
    QCOMPARE(printImageTargetRect(*prepared, 600, 600), QRectF(0, 0, 1200, 2400));
    QCOMPARE(printImageTargetRect(*prepared, 600, 300), QRectF(0, 0, 1200, 1200));
    auto withDisplayScale = *prepared;
    withDisplayScale.image.setDevicePixelRatio(2.5);
    QCOMPARE(printImageTargetRect(withDisplayScale, 300, 300), QRectF(0, 0, 600, 1200));
}

void PrintingTests::rawPreparationPreservesSourceAndQuantity()
{
    const auto source = u"^XA\r\n^CI28^PW600^LL1200^ZZpreserve\r\n"
        u"^FO10,20^A0N,24,0^FDТочный UTF-8^FS^PQ3\r\n^XZ\r\n"_s;
    const auto parsed = fromZpl(source);
    QVERIFY(parsed);
    QVERIFY(!parsed->visualEditable);
    const auto prepared = prepareLabelPrint(*parsed, LabelPrintMode::Zpl, 2);
    QVERIFY2(prepared.has_value(), prepared ? "" : qPrintable(prepared.error()));
    QCOMPARE(prepared->zpl, source.toUtf8());
    QCOMPARE(prepared->labelsPerRepeat, 3);
    QCOMPARE(prepared->repeats, 2);
    const auto bytes = printWireBytes(*prepared);
    QVERIFY(bytes);
    QCOMPARE(*bytes, source.toUtf8()); // Repeats are streamed, never concatenated here.
    QCOMPARE(toZpl(*parsed), source);
    QVERIFY(!prepareLabelPrint(*parsed, LabelPrintMode::Image));
    QVERIFY(!prepareLabelPrint(*parsed, LabelPrintMode::Png));
}

void PrintingTests::templateValuesAreResolvedBeforeEitherMode()
{
    auto document = label();
    document.elements[0].text = u"Партия {{batch}}"_s;
    QVERIFY(!prepareLabelPrint(document, LabelPrintMode::Image));
    QVERIFY(!prepareLabelPrint(document, LabelPrintMode::Zpl));
    QVERIFY(!prepareLabelPrint(document, LabelPrintMode::Png));
    document.previewValues.insert(u"batch"_s, u"B42"_s);
    const auto raw = prepareLabelPrint(document, LabelPrintMode::Zpl);
    QVERIFY2(raw.has_value(), raw ? "" : qPrintable(raw.error()));
    QVERIFY(raw->zpl.contains("B42"));
    QVERIFY(!raw->zpl.contains("{{"));
    const auto image = prepareLabelPrint(document, LabelPrintMode::Image);
    QVERIFY2(image.has_value(), image ? "" : qPrintable(image.error()));
    QVERIFY(!image->image.isNull());
    const auto png = prepareLabelPrint(document, LabelPrintMode::Png);
    QVERIFY2(png.has_value(), png ? "" : qPrintable(png.error()));
    QCOMPARE(png->image, image->image);
    QCOMPARE(document.elements[0].text, u"Партия {{batch}}"_s);
    const auto rawTemplate = fromZpl(u"^XA^PW200^LL100^ZZunknown^FO1,1^FD{{batch}}^FS^XZ"_s);
    QVERIFY(rawTemplate);
    QVERIFY(!rawTemplate->visualEditable);
    QVERIFY(!prepareLabelPrint(*rawTemplate, LabelPrintMode::Zpl));
}

void PrintingTests::imageBecomesOneLosslessGraphicField()
{
    auto document = label();
    document.width = 137;
    document.height = 99;
    const auto job = prepareLabelPrint(document, LabelPrintMode::Image, 3);
    QVERIFY2(job.has_value(), job ? "" : qPrintable(job.error()));
    QCOMPARE(job->labelsPerRepeat, 1);
    const auto wire = printWireBytes(*job);
    QVERIFY2(wire.has_value(), wire ? "" : qPrintable(wire.error()));
    QVERIFY(wire->contains("^GFA,"));
    QCOMPARE(wire->count("^GFA,"), 1);
    QVERIFY(wire->contains("^PW137"));
    QVERIFY(wire->contains("^LL99"));
    const auto parsed = fromZpl(QString::fromUtf8(*wire));
    QVERIFY(parsed);
    QVERIFY(parsed->visualEditable);
    const auto preview = renderPreview(*parsed);
    QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
    QCOMPARE(preview->image.convertToFormat(QImage::Format_Grayscale8), job->image);
}

void PrintingTests::invalidAndMultipleLabelJobsAreRejected()
{
    QVERIFY(!prepareLabelPrint(label(), LabelPrintMode::Image, 0));
    QVERIFY(!prepareLabelPrint(label(), LabelPrintMode::Zpl, 101));
    auto invalid = label();
    invalid.dpi = 96;
    QVERIFY(!prepareLabelPrint(invalid, LabelPrintMode::Image));
    const auto source = toZpl(label());
    const auto multiple = fromZpl(source + source);
    QVERIFY(multiple);
    QVERIFY(!prepareLabelPrint(*multiple, LabelPrintMode::Image));
    QVERIFY(!prepareLabelPrint(*multiple, LabelPrintMode::Png));
    const auto raw = prepareLabelPrint(*multiple, LabelPrintMode::Zpl);
    QVERIFY(raw);
    QCOMPARE(raw->labelsPerRepeat, 2);
    QCOMPARE(raw->zpl, (source + source).toUtf8());
    auto oversized = *raw;
    oversized.zpl = QByteArray(1024 * 1024, 'A');
    oversized.repeats = 100;
    QVERIFY(!printWireBytes(oversized));
}

void PrintingTests::pngPreservesPixelsAndConfiguredDpi()
{
    const auto prepared = prepareLabelPrint(label(), LabelPrintMode::Png, 3);
    QVERIFY2(prepared.has_value(), prepared ? "" : qPrintable(prepared.error()));
    QVERIFY(prepared->zpl.isEmpty());
    QCOMPARE(prepared->labelsPerRepeat, 1);
    const auto originalDpm = prepared->image.dotsPerMeterX();
    PrinterParameters parameters;
    parameters.darkness = 12;
    parameters.speed = 3;
    parameters.labelTop = 5;
    for (const int dpi : {203, 300, 600}) {
        const auto configured = configureLabelPrint(*prepared, dpi, parameters);
        QVERIFY(configured);
        QCOMPARE(configured->pageDots, prepared->pageDots);
        const auto bytes = printWireBytes(*configured);
        QVERIFY2(bytes.has_value(), bytes ? "" : qPrintable(bytes.error()));
        QVERIFY(bytes->startsWith(QByteArray::fromHex("89504e470d0a1a0a")));
        const auto decoded = QImage::fromData(*bytes, "PNG");
        QVERIFY(!decoded.isNull());
        QCOMPARE(decoded.size(), prepared->pageDots);
        QCOMPARE(decoded.convertToFormat(QImage::Format_Grayscale8), prepared->image);
        QCOMPARE(decoded.dotsPerMeterX(), qRound(dpi / 0.0254));
        QCOMPARE(decoded.dotsPerMeterY(), qRound(dpi / 0.0254));
        auto withoutParameters = *configured;
        withoutParameters.printerParameters = {};
        const auto unconfiguredBytes = printWireBytes(withoutParameters);
        QVERIFY(unconfiguredBytes);
        QCOMPARE(*bytes, *unconfiguredBytes); // No ZPL wrapper or printer commands in PNG.
    }
    QCOMPARE(prepared->image.dotsPerMeterX(), originalDpm);
    QCOMPARE(prepared->dpi, 300);
}

void PrintingTests::pngRejectsInvalidJobsAndDriverSubmission()
{
    const auto prepared = prepareLabelPrint(label(), LabelPrintMode::Png);
    QVERIFY(prepared);
    auto invalid = *prepared;
    invalid.image = {};
    QVERIFY(!printWireBytes(invalid));
    invalid = *prepared;
    invalid.pageDots.rwidth() += 1;
    QVERIFY(!printWireBytes(invalid));
    invalid = *prepared;
    invalid.pageDots = QSize(4097, 1);
    invalid.image = QImage(invalid.pageDots, QImage::Format_Grayscale8);
    QVERIFY(!printWireBytes(invalid));
    QVERIFY(!configureLabelPrint(*prepared, 96, {}));
    invalid = *prepared;
    invalid.repeats = 0;
    QVERIFY(!printWireBytes(invalid));
    invalid.mode = static_cast<LabelPrintMode>(3);
    QVERIFY(!printWireBytes(invalid));
    QVERIFY(!prepareLabelPrint(label(), static_cast<LabelPrintMode>(3)));
    PrintDestination installed;
    installed.printerName = u"No printer should be opened"_s;
    const auto submitted = submitLabelPrint(*prepared, installed);
    QVERIFY(!submitted);
    QVERIFY2(submitted.error().contains(u"PNG доступна только по IP"), qPrintable(submitted.error()));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(u"must-not-print.pdf"_s);
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    QVERIFY(!paintLabelPrint(*prepared, printer));
    QVERIFY(!QFile::exists(path));
}

void PrintingTests::pngSizeLimitCountsAllRepeats()
{
    PreparedLabelPrint job;
    job.mode = LabelPrintMode::Png;
    job.pageDots = QSize(1024, 1024);
    job.image = QImage(job.pageDots, QImage::Format_Grayscale8);
    quint32 random = 0x12345678;
    for (int y = 0; y < job.image.height(); ++y) {
        auto* row = job.image.scanLine(y);
        for (int x = 0; x < job.image.width(); ++x) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            row[x] = static_cast<uchar>(random);
        }
    }
    const auto single = printWireBytes(job);
    QVERIFY(single);
    QVERIFY(qint64(single->size()) * 100 > 64 * 1024 * 1024);
    job.repeats = 100;
    const auto oversized = printWireBytes(job);
    QVERIFY(!oversized);
    QVERIFY(oversized.error().contains(u"64 МиБ"));
}

void PrintingTests::imagePrintsToExactSizePdf()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(u"label.pdf"_s);
    const auto job = prepareLabelPrint(label(), LabelPrintMode::Image, 2);
    QVERIFY(job);
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    const auto result = paintLabelPrint(*job, printer);
    QVERIFY2(result.has_value(), result ? "" : qPrintable(result.error()));
    const auto page = printer.pageLayout().fullRect(QPageLayout::Millimeter).size();
    QVERIFY(std::abs(page.width() - 50.8) < 0.25);
    QVERIFY(std::abs(page.height() - 101.6) < 0.25);
    const auto pdf = readBytes(path);
    QVERIFY(pdf.startsWith("%PDF-"));
    QVERIFY(pdf.size() > 1000);
    QCOMPARE(pdf.count("/Type /Page\n"), 2);
}

void PrintingTests::imageFitsOrdinaryPaperAtActualSize()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto job = prepareLabelPrint(label(), LabelPrintMode::Image);
    QVERIFY(job);
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(directory.filePath(u"a4.pdf"_s));
    QVERIFY(printer.setPageSize(QPageSize(QPageSize::A4)));
    const auto result = paintLabelPrint(*job, printer, true);
    QVERIFY2(result.has_value(), result ? "" : qPrintable(result.error()));
    const auto page = printer.pageLayout().fullRect(QPageLayout::Millimeter).size();
    QVERIFY(page.width() > 200);
    QVERIFY(page.height() > 290);
    QCOMPARE(printImageTargetRect(*job, 300, 300), QRectF(0, 0, 600, 1200));
    QVERIFY(!printer.fullPage());
    auto tooLarge = *job;
    tooLarge.pageDots = QSize(4096, 4096);
    tooLarge.image = QImage(tooLarge.pageDots, QImage::Format_Grayscale8);
    tooLarge.image.fill(Qt::white);
    QVERIFY(!paintLabelPrint(tooLarge, printer, true));
}

void PrintingTests::tcpStreamsExactBytesAndRepeats()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    auto document = label();
    document.elements[0].text = u"Сетевая проверка {{batch}}"_s;
    document.previewValues.insert(u"batch"_s, u"TEST"_s);
    const auto job = prepareLabelPrint(document, LabelPrintMode::Zpl, 3);
    QVERIFY(job);
    PrintDestination destination;
    destination.connection = PrintConnection::Tcp;
    destination.host = u"127.0.0.1"_s;
    destination.port = server.serverPort();
    destination.timeoutMs = 5000;
    auto sending = std::async(std::launch::async, [&] { return submitLabelPrint(*job, destination); });
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 4000);
    std::unique_ptr<QTcpSocket> receiver(server.nextPendingConnection());
    QVERIFY(receiver);
    QByteArray received = receiver->readAll();
    connect(receiver.get(), &QTcpSocket::readyRead, this, [&] { received += receiver->readAll(); });
    const QByteArray expected = job->zpl + job->zpl + job->zpl;
    QTRY_COMPARE_WITH_TIMEOUT(received, expected, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(sending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 5000);
    const auto result = sending.get();
    QVERIFY2(result.has_value(), result ? "" : qPrintable(result.error()));
    QCOMPARE(result->bytesAccepted, expected.size());
}

void PrintingTests::tcpConnectionFailureIsReported()
{
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const auto unusedPort = reservation.serverPort();
    reservation.close();
    const auto job = prepareLabelPrint(label(), LabelPrintMode::Zpl);
    QVERIFY(job);
    PrintDestination destination;
    destination.connection = PrintConnection::Tcp;
    destination.host = u"127.0.0.1"_s;
    destination.port = unusedPort;
    destination.timeoutMs = 1000;
    const auto result = submitLabelPrint(*job, destination);
    QVERIFY(!result);
    QVERIFY(result.error().contains(u"Не удалось подключиться"));
    destination.host.clear();
    QVERIFY(!submitLabelPrint(*job, destination));
}

void PrintingTests::pngRepeatsUseSeparateTcpConnections()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const auto job = prepareLabelPrint(label(), LabelPrintMode::Png, 3);
    QVERIFY(job);
    const auto expected = printWireBytes(*job);
    QVERIFY(expected);
    PrintDestination destination;
    destination.connection = PrintConnection::Tcp;
    destination.host = u"127.0.0.1"_s;
    destination.port = server.serverPort();
    destination.timeoutMs = 5000;
    auto sending = std::async(std::launch::async, [&] { return submitLabelPrint(*job, destination); });
    for (int repeat = 0; repeat < job->repeats; ++repeat) {
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
        std::unique_ptr<QTcpSocket> receiver(server.nextPendingConnection());
        QVERIFY(receiver);
        QByteArray received = receiver->readAll();
        connect(receiver.get(), &QTcpSocket::readyRead, this, [&] { received += receiver->readAll(); });
        QTRY_COMPARE_WITH_TIMEOUT(receiver->state(), QAbstractSocket::UnconnectedState, 5000);
        received += receiver->readAll();
        QCOMPARE(received, *expected);
        QCOMPARE(QImage::fromData(received, "PNG").convertToFormat(QImage::Format_Grayscale8), job->image);
    }
    QTRY_VERIFY_WITH_TIMEOUT(sending.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready, 5000);
    const auto result = sending.get();
    QVERIFY2(result.has_value(), result ? "" : qPrintable(result.error()));
    QCOMPARE(result->bytesAccepted, qint64(expected->size()) * job->repeats);
    QVERIFY(!server.hasPendingConnections());
}

void PrintingTests::cancelledDialogDoesNotSaveOrSubmit()
{
    const auto oldName = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(u"printing-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const auto settings = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(u"printing.ini"_s);
    QVERIFY(!QFile::exists(settings));
    PrintDialog dialog(label());
    auto* connection = dialog.findChild<QComboBox*>(u"printDestination"_s);
    auto* mode = dialog.findChild<QComboBox*>(u"printMode"_s);
    auto* host = dialog.findChild<QLineEdit*>(u"printHost"_s);
    QVERIFY(connection && mode && host);
    connection->setCurrentIndex(connection->findData(static_cast<int>(PrintConnection::Tcp)));
    mode->setCurrentIndex(mode->findData(static_cast<int>(LabelPrintMode::Zpl)));
    host->setText(u"127.0.0.1"_s);
    dialog.reject();
    QCOMPARE(dialog.result(), QDialog::Rejected);
    QVERIFY(!QFile::exists(settings));
    QCoreApplication::setApplicationName(oldName);
    QStandardPaths::setTestModeEnabled(false);
}

void PrintingTests::closedBusyDialogCompletesInBackground()
{
    const auto oldName = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(u"printing-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const auto settingsDirectory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const auto settings = QDir(settingsDirectory).filePath(u"printing.ini"_s);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QWidget parent;
    parent.show();
    auto dialog = std::make_unique<PrintDialog>(label(), &parent);
    auto* connection = dialog->findChild<QComboBox*>(u"printDestination"_s);
    auto* mode = dialog->findChild<QComboBox*>(u"printMode"_s);
    auto* host = dialog->findChild<QLineEdit*>(u"printHost"_s);
    auto* port = dialog->findChild<QSpinBox*>(u"printPort"_s);
    auto* submit = dialog->findChild<QPushButton*>(u"printSubmit"_s);
    QVERIFY(connection && mode && host && port && submit);
    connection->setCurrentIndex(connection->findData(static_cast<int>(PrintConnection::Tcp)));
    mode->setCurrentIndex(mode->findData(static_cast<int>(LabelPrintMode::Zpl)));
    host->setText(u"127.0.0.1"_s);
    port->setValue(server.serverPort());
    bool completionSeen = false;
    QString completionText;
    QTimer dismissCompletion;
    connect(&dismissCompletion, &QTimer::timeout, this, [&] {
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* message = qobject_cast<QMessageBox*>(widget)) {
                completionSeen = true;
                completionText = message->text();
                message->accept();
            }
        }
    });
    dismissCompletion.start(10);
    dialog->show();
    submit->click();
    dialog->reject();
    dialog.reset(); // The job must own its snapshot and destination completely.
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 5000);
    std::unique_ptr<QTcpSocket> receiver(server.nextPendingConnection());
    QVERIFY(receiver);
    QByteArray received = receiver->readAll();
    connect(receiver.get(), &QTcpSocket::readyRead, this, [&] { received += receiver->readAll(); });
    const auto prepared = prepareLabelPrint(label(), LabelPrintMode::Zpl);
    QVERIFY(prepared);
    QTRY_COMPARE_WITH_TIMEOUT(received, prepared->zpl, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(completionSeen, 5000);
    QVERIFY2(completionText.contains(u"Передано в"), qPrintable(completionText));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(settings), 5000);
    dismissCompletion.stop();
    QVERIFY(QFile::remove(settings));
    QVERIFY(QDir().rmdir(settingsDirectory));
    QCoreApplication::setApplicationName(oldName);
    QStandardPaths::setTestModeEnabled(false);
}

QTEST_MAIN(PrintingTests)
#include "test_printing.moc"
