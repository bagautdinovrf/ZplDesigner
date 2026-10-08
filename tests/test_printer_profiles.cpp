#include "printer_profiles.hpp"
#include "print_dialog.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
PrinterProfile profile()
{
    PrinterProfile p;
    p.name = u"Склад · этикетки"_s;
    p.destination.connection = PrintConnection::Tcp;
    p.destination.host = u"192.0.2.5"_s;
    p.destination.port = 9200;
    p.destination.timeoutMs = 15000;
    p.destination.printerName = u"Принтер склада"_s;
    p.destination.usePrinterPaper = false;
    p.mode = LabelPrintMode::Zpl;
    p.dpi = 300;
    p.parameters = {12, 4, u"Y"_s, u"T"_s, u"T"_s, -8, 3, -7, 1200};
    return p;
}

QByteArray bytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

void writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

PreparedLabelPrint rawJob(const QByteArray& source)
{
    PreparedLabelPrint job;
    job.mode = LabelPrintMode::Zpl;
    job.zpl = source;
    job.pageDots = {600, 1200};
    job.dpi = 300;
    return job;
}
} // namespace

class PrinterProfileTests final : public QObject {
    Q_OBJECT
private slots:
    void catalogueRoundTripAndDefaults();
    void pngTcpProfileRoundTripKeepsVersionAndParameters();
    void invalidPngConnectionAndUnknownModesAreRejected();
    void invalidCatalogueNeverOverwritesValidData();
    void explicitParametersPreserveUnknownSourceAndCounts();
    void parameterLookingFieldDataAndCommentsAreUntouched();
    void settingsAfterCommentStillOverrideSource();
    void ambiguousSourceRequiresDefaultParameters();
    void dpiChangesOnlyPreparedJobPhysicalSize();
    void imageTcpGetsSettingsAndDriverImageStaysUnchanged();
    void dialogLoadsRenamesAndDeletesProfilesWithoutPrinting();
};

void PrinterProfileTests::catalogueRoundTripAndDefaults()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(u"profiles.json"_s);
    const auto missing = loadPrinterProfiles(path);
    QVERIFY(missing);
    QVERIFY(missing->isEmpty());
    auto defaults = profile();
    defaults.name = u"По умолчанию"_s;
    defaults.mode = LabelPrintMode::Image;
    defaults.dpi = 0;
    defaults.parameters = {};
    QVERIFY(savePrinterProfiles({profile(), defaults}, path));
    const auto loaded = loadPrinterProfiles(path);
    QVERIFY2(loaded.has_value(), loaded ? "" : qPrintable(loaded.error()));
    QCOMPARE(loaded->size(), 2);
    const auto actual = loaded->front();
    const auto expected = profile();
    QCOMPARE(actual.name, expected.name);
    QCOMPARE(actual.destination.connection, expected.destination.connection);
    QCOMPARE(actual.destination.host, expected.destination.host);
    QCOMPARE(actual.destination.port, expected.destination.port);
    QCOMPARE(actual.destination.timeoutMs, expected.destination.timeoutMs);
    QCOMPARE(actual.destination.printerName, expected.destination.printerName);
    QCOMPARE(actual.destination.usePrinterPaper, expected.destination.usePrinterPaper);
    QCOMPARE(actual.mode, expected.mode);
    QCOMPARE(actual.dpi, expected.dpi);
    QVERIFY(actual.parameters == expected.parameters);
    QVERIFY(loaded->back().parameters.isEmpty());
    QCOMPARE(loaded->back().dpi, 0);
    QCOMPARE(loaded->back().mode, LabelPrintMode::Image);
    QVERIFY(savePrinterProfiles({}, path));
    QVERIFY(loadPrinterProfiles(path)->isEmpty());
}

void PrinterProfileTests::pngTcpProfileRoundTripKeepsVersionAndParameters()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(u"profiles.json"_s);
    auto image = profile();
    image.name = u"Растр через драйвер"_s;
    image.destination.connection = PrintConnection::InstalledPrinter;
    image.mode = LabelPrintMode::Image;
    auto png = profile();
    png.name = u"PNG по TCP"_s;
    png.mode = LabelPrintMode::Png;
    QVERIFY(validatePrinterProfile(png).isEmpty());
    QVERIFY(savePrinterProfiles({image, profile(), png}, path));

    // Version 1 keeps the original mode numbers so existing catalogues retain
    // their meaning; PNG adds a third value without rewriting earlier modes.
    const auto json = QJsonDocument::fromJson(bytes(path)).object();
    QCOMPARE(json.value(u"version"_s).toInt(), 1);
    const auto stored = json.value(u"profiles"_s).toArray();
    QCOMPARE(stored.size(), 3);
    for (int index = 0; index < stored.size(); ++index)
        QCOMPARE(stored[index].toObject().value(u"mode"_s).toInt(), index);
    const auto loaded = loadPrinterProfiles(path);
    QVERIFY2(loaded.has_value(), loaded ? "" : qPrintable(loaded.error()));
    QCOMPARE(loaded->size(), 3);
    QCOMPARE(loaded->at(0).mode, LabelPrintMode::Image);
    QCOMPARE(loaded->at(1).mode, LabelPrintMode::Zpl);
    const auto& actual = loaded->last();
    QCOMPARE(actual.mode, LabelPrintMode::Png);
    QCOMPARE(actual.destination.connection, PrintConnection::Tcp);
    QCOMPARE(actual.destination.host, png.destination.host);
    QCOMPARE(actual.destination.port, png.destination.port);
    QCOMPARE(actual.destination.timeoutMs, png.destination.timeoutMs);
    QCOMPARE(actual.dpi, png.dpi);
    QVERIFY(actual.parameters == png.parameters);
}

void PrinterProfileTests::invalidPngConnectionAndUnknownModesAreRejected()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(u"profiles.json"_s);
    auto png = profile();
    png.mode = LabelPrintMode::Png;
    QVERIFY(savePrinterProfiles({png}, path));
    const auto original = bytes(path);

    for (const int mode : {static_cast<int>(LabelPrintMode::Png), -1, 3}) {
        auto invalid = png;
        invalid.mode = static_cast<LabelPrintMode>(mode);
        if (invalid.mode == LabelPrintMode::Png)
            invalid.destination.connection = PrintConnection::InstalledPrinter;
        QVERIFY(!validatePrinterProfile(invalid).isEmpty());
        QVERIFY(!savePrinterProfiles({invalid}, path));
        QCOMPARE(bytes(path), original);

        auto root = QJsonDocument::fromJson(original).object();
        auto item = root.value(u"profiles"_s).toArray().first().toObject();
        item.insert(u"mode"_s, mode);
        item.insert(u"connection"_s, static_cast<int>(invalid.destination.connection));
        root.insert(u"profiles"_s, QJsonArray{item});
        writeBytes(path, QJsonDocument(root).toJson());
        const auto rejected = loadPrinterProfiles(path);
        QVERIFY(!rejected);
        QVERIFY(!rejected.error().isEmpty());
        writeBytes(path, original);
    }
}

void PrinterProfileTests::invalidCatalogueNeverOverwritesValidData()
{
    QTemporaryDir directory;
    const auto path = directory.filePath(u"profiles.json"_s);
    QVERIFY(savePrinterProfiles({profile()}, path));
    const auto original = bytes(path);
    auto invalid = profile();
    invalid.parameters.speed = 100;
    QVERIFY(!savePrinterProfiles({invalid}, path));
    QCOMPARE(bytes(path), original);
    invalid = profile();
    invalid.name = u" \n"_s;
    QVERIFY(!savePrinterProfiles({invalid}, path));
    invalid = profile();
    invalid.parameters.mediaTracking = u"Y^JUS"_s;
    QVERIFY(!savePrinterProfiles({invalid}, path));
    invalid = profile();
    invalid.dpi = 96;
    QVERIFY(!savePrinterProfiles({invalid}, path));
    invalid = profile();
    invalid.name = invalid.name.toUpper();
    QVERIFY(!savePrinterProfiles({profile(), invalid}, path));
    QCOMPARE(bytes(path), original);
    auto corrupt = original;
    corrupt.replace("9200", "65536");
    writeBytes(path, corrupt);
    QVERIFY(!loadPrinterProfiles(path));
    corrupt = original;
    corrupt.replace("\"darkness\": 12", "\"darkness\": 12.5");
    writeBytes(path, corrupt);
    QVERIFY(!loadPrinterProfiles(path));
    writeBytes(path, "{broken}");
    QVERIFY(!loadPrinterProfiles(path));
}

void PrinterProfileTests::explicitParametersPreserveUnknownSourceAndCounts()
{
    const QByteArray source = "~SD2\r\n^XA\r\n^PR2,6,8^LT8^LS9^MD3^ZZunknown,keep\r\n"
        "^FO10,20^FDabc^FS^PQ3\r\n^XZ\r\n^XA^FDsecond^FS^PQ2^XZ";
    auto job = rawJob(source);
    job.repeats = 2;
    job.labelsPerRepeat = 5;
    auto configured = configureLabelPrint(job, 0, profile().parameters);
    QVERIFY(configured);
    const auto wire = printWireBytes(*configured);
    QVERIFY2(wire.has_value(), wire ? "" : qPrintable(wire.error()));
    QVERIFY(wire->startsWith("~SD12\r\n^XA"));
    QCOMPARE(wire->count("^PR4"), 3);
    QVERIFY(wire->contains("^PR4,6,8"));
    QVERIFY(wire->contains("^ZZunknown,keep\r\n"));
    QVERIFY(wire->contains("^PQ3\r\n^XZ\r\n"));
    QVERIFY(wire->endsWith("^PQ2^XZ"));
    QVERIFY(wire->contains("^MD0"));
    QVERIFY(!wire->contains("^MD3"));
    QCOMPARE(configured->repeats, 2);
    QCOMPARE(configured->labelsPerRepeat, 5);
    QCOMPARE(job.zpl, source);
    const auto plain = printWireBytes(job);
    QVERIFY(plain);
    QCOMPARE(*plain, source);
}

void PrinterProfileTests::parameterLookingFieldDataAndCommentsAreUntouched()
{
    const QByteArray field = "^FDliteral ^LT7 ~SD4 ^XA ^XZ^FS";
    const QByteArray comment = "^FXkeep source comment verbatim\r\n";
    auto job = rawJob("^XA" + field + comment + "^BXN,3,200,,,,~^FDdata^FS^XZ");
    job.printerParameters = profile().parameters;
    const auto wire = printWireBytes(job);
    QVERIFY2(wire.has_value(), wire ? "" : qPrintable(wire.error()));
    QVERIFY(wire->contains(field));
    QVERIFY(wire->contains(comment));
    QVERIFY(wire->contains("^BXN,3,200,,,,~^FDdata^FS"));
}

void PrinterProfileTests::settingsAfterCommentStillOverrideSource()
{
    const QByteArray source = "^XA^FXPrinter settings follow\r\n^PR2,6,8^LT8^LS9~SD2"
        "^FO10,20^FDdata^FS^PQ3^XZ";
    auto job = rawJob(source);
    job.printerParameters = profile().parameters;
    const auto wire = printWireBytes(job);
    QVERIFY2(wire.has_value(), wire ? "" : qPrintable(wire.error()));
    QVERIFY(wire->contains("^FXPrinter settings follow\r\n^PR4,6,8^LT3^LS-7~SD12^FO10,20"));
    QVERIFY(wire->contains("^FDdata^FS^PQ3^XZ"));
    QVERIFY(!wire->contains("^PR2"));
    QCOMPARE(job.zpl, source);
}

void PrinterProfileTests::ambiguousSourceRequiresDefaultParameters()
{
    for (const auto& source : {QByteArray("^XA^CC!^FDdata^FS^XZ"), QByteArray("^XA^XFR:file.ZPL^XZ"),
                              QByteArray("^XA^GFB,1,1,1,x^FS^XZ"), QByteArray("^XA^FDunclosed^XZ")}) {
        auto job = rawJob(source);
        const auto original = printWireBytes(job);
        QVERIFY(original);
        QCOMPARE(*original, source);
        job.printerParameters.speed = 3;
        QVERIFY(!printWireBytes(job));
    }
}

void PrinterProfileTests::dpiChangesOnlyPreparedJobPhysicalSize()
{
    auto job = rawJob("^XA^PW600^LL1200^FO1,2^FDdata^FS^XZ");
    const auto configured = configureLabelPrint(job, 600, {});
    QVERIFY(configured);
    QCOMPARE(configured->pageDots, job.pageDots);
    QCOMPARE(configured->zpl, job.zpl);
    QCOMPARE(printPageMillimeters(job), QSizeF(50.8, 101.6));
    QCOMPARE(printPageMillimeters(*configured), QSizeF(25.4, 50.8));
    QCOMPARE(job.dpi, 300);
    QVERIFY(!configureLabelPrint(job, 96, {}));
    PrinterParameters invalid;
    invalid.darkness = 31;
    QVERIFY(!configureLabelPrint(job, 0, invalid));
}

void PrinterProfileTests::imageTcpGetsSettingsAndDriverImageStaysUnchanged()
{
    DesignDocument document;
    document.width = 137;
    document.height = 99;
    document.elements = {makeElement(ElementType::Text, {8, 10})};
    const auto prepared = prepareLabelPrint(document, LabelPrintMode::Image);
    QVERIFY(prepared);
    const auto configured = configureLabelPrint(*prepared, 300, profile().parameters);
    QVERIFY(configured);
    QCOMPARE(configured->image, prepared->image);
    QCOMPARE(configured->pageDots, prepared->pageDots);
    const auto wire = printWireBytes(*configured);
    QVERIFY2(wire.has_value(), wire ? "" : qPrintable(wire.error()));
    QVERIFY(wire->contains("^GFA,"));
    QVERIFY(wire->contains("~SD12"));
    QVERIFY(wire->contains("^MTT"));
    QVERIFY(wire->contains("~TA-8"));
    QCOMPARE(wire->count("^GFA,"), 1);
}

void PrinterProfileTests::dialogLoadsRenamesAndDeletesProfilesWithoutPrinting()
{
    const auto oldName = QCoreApplication::applicationName();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(u"profile-test-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const auto path = printerProfilesPath();
    QVERIFY(savePrinterProfiles({profile()}));
    DesignDocument document;
    document.width = 600; document.height = 1200; document.dpi = 203;
    PrintDialog dialog(document);
    auto* profiles = dialog.findChild<QComboBox*>(u"printProfiles"_s);
    auto* dpi = dialog.findChild<QComboBox*>(u"printDpi"_s);
    auto* host = dialog.findChild<QLineEdit*>(u"printHost"_s);
    auto* repeats = dialog.findChild<QSpinBox*>(u"printRepeats"_s);
    QVERIFY(profiles && dpi && host && repeats);
    repeats->setValue(20);
    profiles->setCurrentIndex(1);
    QCOMPARE(dpi->currentData().toInt(), 300);
    QCOMPARE(host->text(), profile().destination.host);
    QCOMPARE(repeats->value(), 1);
    QCOMPARE(document.dpi, 203);
    const auto* dimensions = dialog.findChild<QLabel*>(u"printDimensions"_s);
    QVERIFY(dimensions->text().contains(u"50.80"));
    QTimer::singleShot(0, &dialog, [] {
        if (auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
            input->setTextValue(u"Новое имя"_s);
            input->accept();
        }
    });
    dialog.findChild<QPushButton*>(u"printRenameProfile"_s)->click();
    QCOMPARE(loadPrinterProfiles()->front().name, u"Новое имя"_s);
    QCOMPARE(profiles->currentText(), u"Новое имя"_s);
    dialog.findChild<QPushButton*>(u"printDeleteProfile"_s)->click();
    QVERIFY(loadPrinterProfiles()->isEmpty());
    QCOMPARE(profiles->currentIndex(), 0);
    dialog.reject();
    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().rmdir(QFileInfo(path).absolutePath()));
    QCoreApplication::setApplicationName(oldName);
    QStandardPaths::setTestModeEnabled(false);
}

QTEST_MAIN(PrinterProfileTests)
#include "test_printer_profiles.moc"
