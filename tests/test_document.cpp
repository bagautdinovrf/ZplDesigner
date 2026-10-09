#include "design_document.hpp"
#include "gs1_data.hpp"
#include "value_placeholders.hpp"

#include <QtZpl/qtzpl.hpp>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

class DocumentTest final : public QObject {
    Q_OBJECT
private slots:
    void sampleRoundTrip();
    void allElementsRoundTrip();
    void fieldEscapingPreservesData();
    void imagePackingAlphaAndPadding();
    void imageThresholdDitherAndResize();
    void unknownCommandsPreserveOriginal();
    void multipleLabelsPreserveOriginalAndPreviewFirst();
    void conservativeImportPreservesSemantics();
    void importedCommentsAndStateSurviveEdits();
    void importedFieldFontsResetAtSeparator();
    void concreteTemplateSpellingRemainsLiteral();
    void productionTemplateSyntaxIsPreserved();
    void malformedAndUnsafeInputs();
    void imageLoading();
    void barcodeErrorsAreVisible();
    void concreteDataMatrixSourceIsReadable_data();
    void concreteDataMatrixSourceIsReadable();
    void legacyHexDataMatrixRemainsEditable_data();
    void legacyHexDataMatrixRemainsEditable();
    void dataMatrixUtf8BytesRoundTrip();
    void gs1AiParsingAndSeparators();
    void gs1ChecksRejectInvalidData();
    void gs1DataMatrixRoundTrip();
    void editorPreviewKeepsOtherElementsAroundInvalidGs1();
    void editorPreviewMarksUnresolvedDataMatrix();
    void editorPreviewDoesNotBypassSafetyOrSourceMode();
    void gs1UnsupportedImportPreservesSource();
    void valueTemplateSyntaxAndResolution();
    void gs1TemplatesValidateKnownData();
    void gs1TemplateResolutionPreservesFieldBoundaries();
    void markerPlaceholdersKeepNamespacesAndBuiltins();
    void rawGs1MarkerTemplatesPreserveAndResolve();
    void exportMustRemainReopenable();
    void resizedElementsRemainPrintableAndReopenable();
    void textResizeRespectsOrientationAndAutomaticWidth();
    void textBlockResizePreservesIndependentAxes();
    void barcodeResizeKeepsCaptionAndModules();
    void resizeLimitsAndUniformAxes();
};

void DocumentTest::sampleRoundTrip()
{
    const auto sample = sampleDocument();
    const QString zpl = toZpl(sample);
    auto loaded = fromZpl(zpl);
    QVERIFY2(loaded.has_value(), qPrintable(loaded ? QString{} : loaded.error()));
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.size(), sample.elements.size());
    QCOMPARE(toZpl(*loaded), zpl);
    loaded->originalZpl.clear();
    const auto before = renderPreview(sample);
    const auto after = renderPreview(*loaded);
    QVERIFY(before.has_value());
    QVERIFY(after.has_value());
    QCOMPARE(before->image, after->image);
    QCOMPARE(before->bounds.size(), sample.elements.size());
    QVERIFY(before->diagnostics.isEmpty());
}

void DocumentTest::allElementsRoundTrip()
{
    DesignDocument original;
    original.width = 700; original.height = 600;
    for (const auto type : {ElementType::Text, ElementType::Box, ElementType::Line,
             ElementType::Ellipse, ElementType::Circle, ElementType::Code128,
             ElementType::QrCode, ElementType::DataMatrix, ElementType::Image}) {
        const int index = int(original.elements.size());
        auto element = makeElement(type, {20 + (index % 3) * 220, 20 + (index / 3) * 180});
        element.gs1 = false;
        element.width = 140;
        element.height = type == ElementType::Line ? 3 : 80;
        element.text = type == ElementType::DataMatrix ? u"A||B|d029"_s : u"AB-123"_s;
        element.moduleWidth = 2;
        if (type == ElementType::Image) {
            element.image = QImage(13, 9, QImage::Format_ARGB32);
            element.image.fill(Qt::white);
            element.image.setPixelColor(2, 3, Qt::black);
            element.width = 13; element.height = 9;
        }
        original.elements.append(element);
    }
    auto imported = fromZpl(toZpl(original));
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    QCOMPARE(imported->elements.size(), original.elements.size());
    QCOMPARE(imported->elements[7].text, original.elements[7].text);
    imported->originalZpl.clear();
    const auto a = renderPreview(original);
    const auto b = renderPreview(*imported);
    QVERIFY(a.has_value()); QVERIFY(b.has_value());
    QCOMPARE(a->image, b->image);
    QVERIFY2(a->diagnostics.isEmpty(), qPrintable(a->diagnostics.join(u'\n')));
}

void DocumentTest::fieldEscapingPreservesData()
{
    DesignDocument document;
    auto text = makeElement(ElementType::Text);
    text.text = u"Привет ^FS^XZ ~JA _00\n\r\t"_s + QChar(0);
    document.elements.append(text);
    const QString source = toZpl(document);
    QCOMPARE(source.count(u"^XZ"_s), 1);
    QCOMPARE(source.count(u"^FS"_s), 1);
    QVERIFY(source.contains(u"^CI28"));
    QVERIFY(source.contains(u"_5EFS_5EXZ"));
    QVERIFY(source.contains(u"_7EJA _5F00_0A_0D_09_00"));
    QVERIFY(source.contains(u"Привет"));
    auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    QCOMPARE(imported->elements.first().text, text.text);
    imported->originalZpl.clear();
    QCOMPARE(toZpl(*imported), source);
}

void DocumentTest::imagePackingAlphaAndPadding()
{
    Element image = makeElement(ElementType::Image, {3, 4});
    image.width = 9; image.height = 2;
    image.image = QImage(9, 2, QImage::Format_ARGB32);
    image.image.fill(Qt::transparent);
    image.image.setPixelColor(0, 0, Qt::black);
    image.image.setPixelColor(7, 0, Qt::black);
    image.image.setPixelColor(8, 0, Qt::black);
    image.image.setPixelColor(0, 1, QColor(127, 127, 127));
    image.image.setPixelColor(1, 1, QColor(128, 128, 128));
    const QString zpl = elementZpl(image);
    QVERIFY2(zpl.contains(u"^GFA,4,4,2,81808000"), qPrintable(zpl));
    DesignDocument document;
    document.width = 32; document.height = 20;
    document.elements = {image};
    const auto preview = renderPreview(document);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->image.pixelColor(3, 4), QColor(Qt::black));
    QCOMPARE(preview->image.pixelColor(4, 4), QColor(Qt::white));
    QCOMPARE(preview->image.pixelColor(11, 4), QColor(Qt::black));
    QCOMPARE(preview->image.pixelColor(12, 4), QColor(Qt::white));
    QCOMPARE(preview->image.pixelColor(3, 5), QColor(Qt::black));
    QCOMPARE(preview->image.pixelColor(4, 5), QColor(Qt::white));
    QCOMPARE(preview->bounds.first(), QRect(3, 4, 9, 2));
    auto imported = fromZpl(toZpl(document));
    QVERIFY(imported.has_value());
    QVERIFY(imported->visualEditable);
    QCOMPARE(imported->elements.first().width, 16);
    imported->originalZpl.clear();
    QCOMPARE(renderPreview(*imported)->image, preview->image);
}

void DocumentTest::imageThresholdDitherAndResize()
{
    auto element = makeElement(ElementType::Image);
    element.width = 16; element.height = 16;
    element.image = QImage(2, 2, QImage::Format_ARGB32);
    element.image.fill(QColor(128, 128, 128));
    const auto plain = monochromeImage(element);
    QCOMPARE(plain.size(), QSize(16, 16));
    QCOMPARE(plain.pixelColor(0, 0), QColor(Qt::white));
    element.dither = true;
    const auto dithered = monochromeImage(element);
    int black = 0;
    for (int y = 0; y < dithered.height(); ++y)
        for (int x = 0; x < dithered.width(); ++x) {
            const int gray = qGray(dithered.pixel(x, y));
            QVERIFY(gray == 0 || gray == 255);
            black += gray == 0;
        }
    QVERIFY(black > 100 && black < 156);
    element.width = 50000;
    QVERIFY(monochromeImage(element).isNull());
}

void DocumentTest::unknownCommandsPreserveOriginal()
{
    const QString source = u"\r\n^XA\r\n^PW320^LL160^FO10,10^A0N,20,20^FDKeep me^FS^ZZsomething^XZ\r\n"_s;
    const auto document = fromZpl(source);
    QVERIFY(document.has_value());
    QVERIFY(!document->visualEditable);
    QVERIFY(document->elements.isEmpty());
    QVERIFY(!document->importNotes.isEmpty());
    QCOMPARE(toZpl(*document), source);
    const auto preview = renderPreview(*document);
    QVERIFY(preview.has_value());
    QVERIFY(!preview->diagnostics.isEmpty());
}

void DocumentTest::multipleLabelsPreserveOriginalAndPreviewFirst()
{
    const QString source = u"^XA^PW64^LL32^FO1,1^GB10,10,2^FS^XZ\n^XA^PW128^LL64^FO20,20^GB8,8,2^FS^XZ\n"_s;
    const auto document = fromZpl(source);
    QVERIFY(document.has_value());
    QVERIFY(!document->visualEditable);
    QCOMPARE(toZpl(*document), source);
    const auto preview = renderPreview(*document);
    QVERIFY(preview.has_value());
    QCOMPARE(preview->image.size(), QSize(64, 32));
    QCOMPARE(preview->image.pixelColor(1, 1), QColor(Qt::black));
    QVERIFY(!preview->diagnostics.isEmpty());
}

void DocumentTest::conservativeImportPreservesSemantics()
{
    const QStringList sources{
        u"^XA^PW200^LL100^LH10,20^FO5,5^GB20,20,2^FS^XZ"_s,
        u"^XA^PW200^LL100^FT,40^A0N,20,0^FDText^FS^XZ"_s,
        u"^XA^PW200^LL100^FO20,40^AAN,20,10^FDText^FS^XZ"_s,
        u"^XA^PW200^LL100^PQ3^FO5,5^GB20,20,2^FS^XZ"_s,
        u"~DGR:LOGO.GRF,1,1,80^XA^PW200^LL100^FO5,5^GB20,20,2^FS^XZ"_s,
        u"^XA^CI28^PW300^LL100^FH_^FO0,0^A0N,20,20^FDOne^FS^FO0,40^FD_41^FS^XZ"_s,
        u"^XA^CI28^PW300^LL100^FB200,1,0,C,0^FO0,0^A0N,20,20^FDOne^FS^FO0,40^FDTwo^FS^XZ"_s,
        u"^XA^CI28^PW300^LL100^FR^FO0,0^A0N,20,20^FDOne^FS^FO0,40^FDTwo^FS^XZ"_s,
        u"^XA^PW300^LL200^FO10,10^A0N,30,25^BY2^BCN,80,Y^FD123456^FS^XZ"_s,
    };
    for (const QString& source : sources) {
        const auto imported = fromZpl(source);
        QVERIFY2(imported.has_value(), qPrintable(source));
        QVERIFY2(!imported->visualEditable, qPrintable(source));
        QCOMPARE(toZpl(*imported), source);
    }
    const auto plain = fromZpl(u"^XA^PW200^LL100^FO10,10^A0N,20,0^FDHello^FS^FO10,40^GB100,30,2^FS^XZ"_s);
    QVERIFY(plain.has_value());
    QVERIFY2(plain->visualEditable, qPrintable(plain->importNotes.join(u'\n')));
    QCOMPARE(plain->elements.size(), 2);
}

void DocumentTest::malformedAndUnsafeInputs()
{
    const QStringList sources{
        u"not zpl"_s,
        u"^XA^FO1,1^FDmissing end^FS"_s,
        u"^XA^XA^XZ^XZ"_s,
        u"^XA^PW100000^LL100000^XZ"_s,
        u"^XA^PW-1^LL100^XZ"_s,
        u"^XA^PWxyz^LL100^XZ"_s,
        u"^XA^FO0,0^GFA,2147483647,2147483647,1,00^FS^XZ"_s,
        u"^XA^FO0,0^A0N,2147483647,0^FDX^FS^XZ"_s,
        u"^XA^FO0,0^BY100000^BCN,100^FD123^FS^XZ"_s,
        u"^XA^FO0,0^BQN,2,100000^FDLA,123^FS^XZ"_s,
        u"^XA^FO0,0^A0N,512,512^FD"_s + QString(1024, u'A') + u"^FS^XZ"_s,
    };
    for (const auto& source : sources) QVERIFY2(!fromZpl(source), qPrintable(source.left(100)));
    QVERIFY(!fromZpl(QString(8 * 1024 * 1024 + 1, u'A')));
    QVERIFY(!fromZpl(u"^XA^FO0,0^GFA,1,1,1,:Z64:"_s
        + QString(17000, u'A') + u":0000^FS^XZ"_s));
    auto document = sampleDocument();
    document.width = 100000;
    QVERIFY(!renderPreview(document));
    document = sampleDocument();
    document.elements.first().x = 2147483647;
    QVERIFY(!renderPreview(document));
}

void DocumentTest::importedCommentsAndStateSurviveEdits()
{
    const QString source = u"^XA\r\n^CI28^PW600^LL300^LH0,0^PON^CF0,24,0\r\n"
        "^FX Keep the label instructions\r\n"
        "^FO10,10^A0N,24,0^FDFirst^FS\r\n"
        "^FX Keep the second field note^FS\r\n"
        "^FO10,60^FDSecond^FS\r\n"
        "^BY2,2,60^FO10,100^BCN,,N,N,N,N^FD123456^FS\r\n"
        "^FO10,200^BCN,,N,N,N,N^FD789012^FS\r\n^PQ1^XZ\r\n"_s;
    auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    QCOMPARE(imported->elements.size(), 4);
    QCOMPARE(imported->elements[1].fontHeight, 24);
    QCOMPARE(imported->elements[3].height, 60);
    QCOMPARE(imported->elements[3].barcodeRatio, 2.0);
    QCOMPARE(toZpl(*imported), source);
    imported->originalZpl.clear();
    QCOMPARE(toZpl(*imported), source);

    imported->elements[0].fontHeight = 44;
    imported->elements[2].moduleWidth = 3;
    imported->elements[2].height = 80;
    const auto edited = toZpl(*imported);
    QVERIFY(edited.contains(u"^FX Keep the label instructions\r\n"));
    QVERIFY(edited.contains(u"^FX Keep the second field note^FS\r\n"));
    QVERIFY(edited.contains(u"^FO10,60^FDSecond^FS"));
    QVERIFY(edited.endsWith(u"^PQ1^XZ\r\n"));
    auto reopened = fromZpl(edited);
    QVERIFY(reopened.has_value());
    QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
    QCOMPARE(reopened->elements[0].fontHeight, 44);
    QCOMPARE(reopened->elements[1].fontHeight, 24);
    QCOMPARE(reopened->elements[2].height, 80);
    QCOMPARE(reopened->elements[3].height, 60);
    QCOMPARE(reopened->elements[3].moduleWidth, 2);

    // Removing or reordering the field that established font/BY state must
    // not change a remaining field's inherited effective parameters.
    imported->elements.removeFirst();
    std::swap(imported->elements[0], imported->elements[2]);
    imported->width = 640;
    reopened = fromZpl(toZpl(*imported));
    QVERIFY(reopened.has_value());
    QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
    QCOMPARE(reopened->width, 640);
    QCOMPARE(reopened->elements[0].text, u"789012"_s);
    QCOMPARE(reopened->elements[0].moduleWidth, 2);
    QCOMPARE(reopened->elements[0].height, 60);
    QCOMPARE(reopened->elements[2].text, u"Second"_s);
    QCOMPARE(reopened->elements[2].fontHeight, 24);

    auto lateDimensions = fromZpl(u"^XA^PW100^LL100^CF0,20,20^FO0,0^A0N,20,20^FDOne^FS"
        "^PW\n200^LL200^FO0,30^FDTwo^FS^XZ"_s);
    QVERIFY(lateDimensions.has_value());
    QVERIFY(lateDimensions->visualEditable);
    lateDimensions->originalZpl.clear();
    lateDimensions->elements.removeLast();
    const auto keptDimensions = fromZpl(toZpl(*lateDimensions));
    QVERIFY(keptDimensions.has_value());
    QVERIFY(keptDimensions->visualEditable);
    QCOMPARE(keptDimensions->width, 200);
    QCOMPARE(keptDimensions->height, 200);
}

void DocumentTest::importedFieldFontsResetAtSeparator()
{
    const auto source = u"^XA^PW300^LL150^CF0,24,0"
        "^FO10,10^A0N,44,0^FDFirst^FS^FO10,80^FDSecond^FS^XZ"_s;
    auto imported = fromZpl(source);
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    QCOMPARE(imported->elements[0].fontHeight, 44);
    QCOMPARE(imported->elements[1].fontHeight, 24);
    imported->originalZpl.clear();
    imported->elements.removeFirst();
    const auto reopened = fromZpl(toZpl(*imported));
    QVERIFY(reopened.has_value());
    QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
    QCOMPARE(reopened->elements.first().fontHeight, 24);

    const auto bitmapDefault = u"^XA^PW300^LL150^FO10,10^A0N,44,0^FDFirst^FS"
        "^FO10,80^FDSecond^FS^XZ"_s;
    const auto preserved = fromZpl(bitmapDefault);
    QVERIFY(preserved.has_value());
    QVERIFY(!preserved->visualEditable);
    QCOMPARE(toZpl(*preserved), bitmapDefault);
}

void DocumentTest::concreteTemplateSpellingRemainsLiteral()
{
    for (const auto type : {ElementType::Text, ElementType::Code128, ElementType::QrCode}) {
        for (const auto& value : {u"{{literal}} [[literal]]"_s, u"tail}}"_s, u"tail]]"_s}) {
            DesignDocument document;
            auto element = makeElement(type, {20, 20});
            element.moduleWidth = 1;
            element.text = value;
            document.elements = {element};
            const auto concrete = toZpl(document, false);
            QVERIFY(!concrete.contains(u"{{"));
            QVERIFY(!concrete.contains(u"[["));
            QVERIFY(!concrete.contains(u"}}"));
            QVERIFY(!concrete.contains(u"]]"));
            auto loaded = fromZpl(concrete);
            QVERIFY(loaded.has_value());
            QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
            QCOMPARE(loaded->elements.first().text, value);
            QVERIFY(loaded->elements.first().literalText);
            QVERIFY(renderPreview(*loaded).has_value());
            loaded->originalZpl.clear();
            ++loaded->elements.first().x;
            const auto moved = toZpl(*loaded);
            QVERIFY(!moved.contains(u"{{"));
            QVERIFY(!moved.contains(u"[["));
            const auto reopened = fromZpl(moved);
            QVERIFY(reopened.has_value());
            QVERIFY(reopened->visualEditable);
            QCOMPARE(reopened->elements.first().text, value);
            QVERIFY(reopened->elements.first().literalText);
        }
    }

    DesignDocument document;
    auto text = makeElement(ElementType::Text);
    text.text = u"Name: {{serial_AB}}\\&"_s;
    text.blockWidth = 300;
    document.elements = {text};
    const auto source = toZpl(document);
    QVERIFY(source.contains(u"{{serial_AB}}"));
    const auto loaded = fromZpl(source);
    QVERIFY(loaded.has_value());
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.first().text, text.text);
    QCOMPARE(loaded->elements.first().blockWidth, 300);
    QVERIFY(!loaded->elements.first().literalText);
}

void DocumentTest::productionTemplateSyntaxIsPreserved()
{
    // Representative syntax from marker INI presets, with synthetic field text.
    // The tiny Z64 graphic is an existing fixture's standalone bitonal data.
    const QString source = u"^XA\n^PW600^LL400^LS0\n"
        "^FT20,80^A@N,25,0,ARIALR.TTF^FH\\^CI28^FD[[NAME]] {{CODE}}^FS^CI27\n"
        "^FT0,120^A0N,25,26^FB600,1,0,C^FH\\^CI28^FDTemplate {{SERIES}}^FS\n"
        "^FO20,180^GFA,37,416,52,:Z64:eJxjYCAd/CcRHBjVA9ZDDgAAN9EjcA==:E64F\n"
        "^PQ1,0,1,Y^XZ\n"_s;
    const auto imported = fromZpl(source);
    QVERIFY2(imported.has_value(), qPrintable(imported ? QString{} : imported.error()));
    QVERIFY(!imported->visualEditable);
    QCOMPARE(toZpl(*imported), source);
    QVERIFY(imported->importNotes.join(u'\n').contains(u"{{…}}"));
    QVERIFY(imported->importNotes.join(u'\n').contains(u"external-font-preview"));
    const auto preview = renderPreview(*imported);
    QVERIFY2(preview.has_value(), qPrintable(preview ? QString{} : preview.error()));
    QCOMPARE(preview->image.size(), QSize(600, 400));
    QVERIFY(!preview->diagnostics.join(u'\n').contains(u"graphic-z64"));
}

void DocumentTest::imageLoading()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(u"input.png"_s);
    QImage image(9, 7, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    image.setPixelColor(4, 3, Qt::black);
    QVERIFY(image.save(path));
    const auto loaded = loadImage(path);
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->size(), image.size());
    QCOMPARE(loaded->pixelColor(0, 0), QColor(Qt::transparent));
    QCOMPARE(loaded->pixelColor(4, 3), QColor(Qt::black));
    QVERIFY(!loadImage(directory.filePath(u"missing.png"_s)));
    const QString invalidPath = directory.filePath(u"invalid.png"_s);
    QFile invalid(invalidPath);
    QVERIFY(invalid.open(QIODevice::WriteOnly));
    invalid.write("invalid image"); invalid.close();
    QVERIFY(!loadImage(invalidPath));
}

void DocumentTest::barcodeErrorsAreVisible()
{
    DesignDocument document;
    auto code = makeElement(ElementType::Code128);
    code.text = QStringLiteral("Кириллица");
    document.elements = {code};
    const auto preview = renderPreview(document);
    QVERIFY(!preview.has_value());
    QVERIFY(preview.error().contains(u"code128-encode"));
    const auto editor = renderEditorPreview(document);
    QVERIFY(editor.has_value());
    QVERIFY(editor->elementErrors.contains(0));
}

void DocumentTest::concreteDataMatrixSourceIsReadable_data()
{
    QTest::addColumn<bool>("gs1");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("field");
    QTest::newRow("requested-gs1") << true << u"(01)09506000134352(21)NL-00001"_s
        << u"^FO612,180^BXN,6,200,0,0,1,|^FD|1010950600013435221NL-00001^FS\n"_s;
    QTest::newRow("ordinary-ascii") << false << u"NL-00001"_s
        << u"^FO612,180^BXN,6,200,0,0,0,|^FDNL-00001^FS\n"_s;
    QTest::newRow("literal-underscore-without-fh") << false << u"SN_AB_1D"_s
        << u"^FO612,180^BXN,6,200,0,0,0,|^FDSN_AB_1D^FS\n"_s;
    QTest::newRow("literal-fnc1-marker") << false << u"A|1|d029B"_s
        << u"^FO612,180^BXN,6,200,0,0,0,|^FDA||1||d029B^FS\n"_s;
    QTest::newRow("gs-separator-and-literal-underscores") << true
        << u"(01)09506000134352(10)LOT_1D(21)SN_AB_1D"_s
        << u"^FO612,180^BXN,6,200,0,0,1,|^FH_^FD|1010950600013435210LOT_5F1D_1D21SN_5FAB_5F1D^FS\n"_s;
    QTest::newRow("reserved-command-bytes") << false << u"^FS~JA_SN_AB_1D|1"_s
        << u"^FO612,180^BXN,6,200,0,0,0,|^FH_^FD_5EFS_7EJA_5FSN_5FAB_5F1D||1^FS\n"_s;
    QTest::newRow("controls") << false << (u"A"_s + QChar(0x1d) + u"B\n\r\t"_s + QChar(0) + QChar(127))
        << u"^FO612,180^BXN,6,200,0,0,0,|^FH_^FDA_1DB_0A_0D_09_00_7F^FS\n"_s;
    // Keep the existing byte-oriented UTF-8 representation. Mixed readable
    // ASCII/FH output is used only when there are no non-ASCII payload bytes.
    QTest::newRow("utf8-keeps-full-hex") << false << u"é A|1^FS~_1D"_s
        << u"^FO612,180^BXN,6,200,0,0,0,|^FH_^FD_C3_A9_20_41_7C_7C_31_5E_46_53_7E_5F_31_44^FS\n"_s;
}

void DocumentTest::concreteDataMatrixSourceIsReadable()
{
    QFETCH(bool, gs1);
    QFETCH(QString, text);
    QFETCH(QString, field);
    auto code = makeElement(ElementType::DataMatrix, {612, 180});
    code.gs1 = gs1;
    code.moduleWidth = 6;
    code.text = text;
    QCOMPARE(elementZpl(code), field);
    QCOMPARE(elementZpl(code, false), field);
    DesignDocument document;
    document.elements = {code};
    const auto source = toZpl(document);
    const auto parsed = QtZpl::parse(source);
    QVERIFY(parsed.has_value());
    QString payload = text;
    if (gs1) {
        const auto data = parseGs1(text);
        QVERIFY(data.has_value());
        payload = data->elementString;
    }
    payload.replace(u"|"_s, u"||"_s);
    if (gs1) payload.prepend(u"|1"_s);
    int fields = 0;
    for (const auto& command : parsed->labels().first().commands()) {
        if (const auto* data = std::get_if<QtZpl::FieldData>(&command.payload)) {
            // QtZpl decodes ^CI28/^FH into Unicode; compare its UTF-8 bytes.
            QCOMPARE(data->data.toUtf8(), payload.toUtf8());
            ++fields;
        }
    }
    QCOMPARE(fields, 1);
    auto loaded = fromZpl(source);
    QVERIFY(loaded.has_value());
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.size(), 1);
    QCOMPARE(loaded->elements.first().gs1, gs1);
    QCOMPARE(loaded->elements.first().text, text);
    QCOMPARE(toZpl(*loaded), source);
    loaded->originalZpl.clear();
    QCOMPARE(toZpl(*loaded), source);
    const auto before = renderPreview(document);
    const auto after = renderPreview(*loaded);
    QVERIFY(before.has_value());
    QVERIFY(after.has_value());
    QCOMPARE(after->image, before->image);
}

void DocumentTest::legacyHexDataMatrixRemainsEditable_data()
{
    QTest::addColumn<bool>("gs1");
    QTest::addColumn<QString>("text");
    QTest::newRow("ordinary") << false << u"SN_AB_1D|1"_s;
    QTest::newRow("gs1-with-separator") << true << u"(01)09506000134352(10)LOT1(21)SN_AB_1D"_s;
}

void DocumentTest::legacyHexDataMatrixRemainsEditable()
{
    QFETCH(bool, gs1);
    QFETCH(QString, text);
    QString payload = gs1 ? parseGs1(text)->elementString : text;
    payload.replace(u"|"_s, u"||"_s);
    if (gs1) payload.prepend(u"|1"_s);
    QString fullHex;
    for (const unsigned char byte : payload.toUtf8())
        fullHex += u'_' + QString::number(byte, 16).rightJustified(2, u'0').toUpper();
    const QString legacy = u"^XA\n^CI28\n^PW812\n^LL406\n^LH0,0\n^FO24,24^BXN,3,200,0,0,%1,|^FH_^FD%2^FS\n^XZ\n"_s
        .arg(gs1 ? 1 : 0).arg(fullHex);
    auto loaded = fromZpl(legacy);
    QVERIFY(loaded.has_value());
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->originalZpl, legacy);
    QCOMPARE(toZpl(*loaded), legacy);
    QCOMPARE(loaded->elements.first().text, text);
    QCOMPARE(loaded->elements.first().gs1, gs1);
    const auto original = renderPreview(*loaded);
    QVERIFY(original.has_value());
    loaded->originalZpl.clear();
    const auto regenerated = toZpl(*loaded);
    QCOMPARE(regenerated, legacy); // Clearing the cache does not normalize untouched fields.
    const auto regeneratedPreview = renderPreview(*loaded);
    QVERIFY(regeneratedPreview.has_value());
    QCOMPARE(regeneratedPreview->image, original->image);

    // A subsequent visual move regenerates the field in the new format but
    // must still encode the same bytes as an equivalently moved legacy field.
    loaded->elements.first().x += 8;
    QVERIFY(!toZpl(*loaded).contains(u"^FD_7C_31"_s));
    QString movedLegacy = legacy;
    movedLegacy.replace(u"^FO24,24"_s, u"^FO32,24"_s);
    const auto moved = fromZpl(movedLegacy);
    QVERIFY(moved.has_value());
    QVERIFY(moved->visualEditable);
    const auto movedOldPreview = renderPreview(*moved);
    const auto movedNewPreview = renderPreview(*loaded);
    QVERIFY(movedOldPreview.has_value());
    QVERIFY(movedNewPreview.has_value());
    QCOMPARE(movedNewPreview->image, movedOldPreview->image);
    const auto reopened = fromZpl(toZpl(*loaded));
    QVERIFY(reopened.has_value());
    QVERIFY(reopened->visualEditable);
    QCOMPARE(reopened->elements.first().x, 32);
    QCOMPARE(reopened->elements.first().text, text);
}

void DocumentTest::dataMatrixUtf8BytesRoundTrip()
{
    DesignDocument document;
    auto element = makeElement(ElementType::DataMatrix, {40, 40});
    element.gs1 = false;
    element.text = u"Код é |1 ^FS"_s;
    document.elements = {element};
    const QString source = toZpl(document);
    const auto expectedBytes = QByteArray::fromHex("D09AD0BED0B420C3A9207C7C31205E4653");
    const auto parsed = QtZpl::parse(source);
    QVERIFY(parsed.has_value());
    QString escapedMarkers = element.text;
    escapedMarkers.replace(u"|"_s, u"||"_s);
    bool found = false;
    for (const auto& command : parsed->labels().first().commands())
        if (const auto* data = std::get_if<QtZpl::FieldData>(&command.payload)) {
            QCOMPARE(data->data, escapedMarkers);
            QCOMPARE(data->data.toUtf8(), expectedBytes);
            QVERIFY(data->bytes.has_value());
            QCOMPARE(*data->bytes, expectedBytes);
            QCOMPARE(command.source, u"^FD_D0_9A_D0_BE_D0_B4_20_C3_A9_20_7C_7C_31_20_5E_46_53"_s);
            found = true;
        }
    QVERIFY(found);
    auto loaded = fromZpl(source);
    QVERIFY(loaded.has_value());
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.first().text, element.text);
    loaded->originalZpl.clear();
    QCOMPARE(toZpl(*loaded), source);

    // Moving forces field regeneration instead of reusing the imported bytes.
    loaded->elements.first().x += 8;
    QString movedSource = source;
    movedSource.replace(u"^FO40,40"_s, u"^FO48,40"_s);
    // Regenerated fields restore ^CI28 and may change inter-command whitespace.
    // Require the same data bytes and moved geometry, independently of spelling.
    const QString movedZpl = toZpl(*loaded);
    const auto movedParsed = QtZpl::parse(movedZpl);
    QVERIFY(movedParsed.has_value());
    int movedFields = 0;
    int movedOrigins = 0;
    for (const auto& command : movedParsed->labels().first().commands()) {
        if (const auto* data = std::get_if<QtZpl::FieldData>(&command.payload)) {
            ++movedFields;
            QCOMPARE(data->data, escapedMarkers);
            QVERIFY(data->bytes.has_value());
            QCOMPARE(*data->bytes, expectedBytes);
        } else if (const auto* origin = std::get_if<QtZpl::FieldOrigin>(&command.payload)) {
            ++movedOrigins;
            QCOMPARE(origin->x, 48);
            QCOMPARE(origin->y, 40);
            QCOMPARE(command.source, u"^FO48,40"_s);
        }
    }
    QCOMPARE(movedFields, 1);
    QCOMPARE(movedOrigins, 1);
    const auto reopened = fromZpl(movedZpl);
    QVERIFY(reopened.has_value());
    QVERIFY2(reopened->visualEditable, qPrintable(reopened->importNotes.join(u'\n')));
    QCOMPARE(reopened->elements.size(), 1);
    QCOMPARE(reopened->elements.first().x, 48);
    QCOMPARE(reopened->elements.first().text, element.text);

    const auto preview = renderPreview(*reopened);
    QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
    QVERIFY2(preview->diagnostics.isEmpty(), qPrintable(preview->diagnostics.join(u'\n')));
    const auto movedExpected = fromZpl(movedSource);
    QVERIFY(movedExpected.has_value());
    QVERIFY(movedExpected->visualEditable);
    const auto expectedPreview = renderPreview(*movedExpected);
    QVERIFY(expectedPreview.has_value());
    QVERIFY(expectedPreview->diagnostics.isEmpty());
    QCOMPARE(preview->image, expectedPreview->image);
}

void DocumentTest::gs1AiParsingAndSeparators()
{
    const QString human = u"(01)09504000059118(17)271231(10)LOT42(21)SERIAL-7"_s;
    const auto parsed = parseGs1(human);
    QVERIFY2(parsed.has_value(), qPrintable(parsed ? QString{} : parsed.error()));
    QCOMPARE(parsed->canonicalText, human);
    QCOMPARE(parsed->fields.size(), 4);
    QCOMPARE(parsed->elementString, u"01095040000591181727123110LOT42"_s + QChar(0x1d) + u"21SERIAL-7"_s);
    QCOMPARE(parsed->elementString.count(QChar(0x1d)), 1);
    const auto scanned = parseGs1(u"]d2"_s + parsed->elementString);
    QVERIFY(scanned.has_value());
    QCOMPARE(scanned->fields, parsed->fields);
    QString visibleGs = parsed->elementString;
    visibleGs.replace(QChar(0x1d), u"<GS>"_s);
    QCOMPARE(parseGs1(visibleGs)->canonicalText, human);

    // Even a variable-length value at its maximum length needs a separator
    // before another AI. Never infer a boundary merely from a numeric suffix.
    const auto maximumLot = buildGs1({{u"01"_s, u"09504000059118"_s},
        {u"10"_s, QString(20, u'A')}, {u"21"_s, u"123"_s}});
    QVERIFY(maximumLot.has_value());
    QVERIFY(maximumLot->elementString.contains(QString(20, u'A') + QChar(0x1d) + u"21123"_s));
    QCOMPARE(parseGs1(maximumLot->elementString)->fields, maximumLot->fields);
    const auto fixed = parseGs1(u"(01)09504000059118(3103)001234(17)270200"_s);
    QVERIFY(fixed.has_value());
    QVERIFY(!fixed->elementString.contains(QChar(0x1d)));
    QCOMPARE(parseGs1(fixed->elementString)->fields, fixed->fields);

    // Parentheses belong to character set X and must survive the human form.
    const auto punctuation = buildGs1({{u"01"_s, u"09504000059118"_s},
        {u"10"_s, u"LOT(A)<GS>"_s}});
    QVERIFY(punctuation.has_value());
    QCOMPARE(parseGs1(punctuation->canonicalText)->fields, punctuation->fields);
    QCOMPARE(parseGs1(punctuation->elementString, false)->fields, punctuation->fields);
    QVERIFY(punctuation->canonicalText.contains(u"LOT\\(A\\)<GS>"));
    QList<Gs1Field> escapedFields{{u"01"_s, u"09504000059118"_s}};
    for (int ai = 91; ai <= 99; ++ai)
        escapedFields.append({QString::number(ai), QString(90, u'(')});
    for (const auto ai : {u"90", u"240", u"241", u"250", u"251", u"400"})
        escapedFields.append({QString(ai), QString(30, u'(')});
    for (const auto ai : {u"10", u"21", u"22", u"420"})
        escapedFields.append({QString(ai), QString(20, u'(')});
    const auto heavilyEscaped = buildGs1(escapedFields);
    QVERIFY(heavilyEscaped.has_value());
    QVERIFY(heavilyEscaped->canonicalText.size() > 2012);
    const auto reparsed = parseGs1(heavilyEscaped->canonicalText);
    QVERIFY(reparsed.has_value());
    QCOMPARE(reparsed->fields, escapedFields);
}

void DocumentTest::gs1ChecksRejectInvalidData()
{
    const QStringList invalid{
        u""_s,
        u"(21)NO-KEY"_s,
        u"(01)09504000059119"_s, // Wrong GTIN check digit.
        u"(01)0950400005911"_s,
        u"(01)09504000059118(21)"_s,
        u"(01)09504000059118(10)"_s + QString(21, u'A'),
        u"(01)09504000059118(21)Партия"_s,
        u"(01)09504000059118(21)A B"_s,
        u"(01)09504000059118(21)ABC^FS"_s,
        u"(01)09504000059118(17)270229"_s,
        u"(01)09504000059118(17)271300"_s,
        u"(01)09504000059118(17)270000"_s,
        u"(01)09504000059118(30)１２３"_s,
        u"(01)09504000059118(21)A(21)B"_s,
        u"(01)09504000059118(3100)000001(3101)000001"_s,
        u"(01)09504000059118(02)09504000059118"_s,
        u"(01)09504000059118(37)12"_s,
        u"(01)09504000059118(250)SECONDARY"_s,
        u"(01)09504000059118(9999)unknown"_s,
        u"010950400005911810LOT<GS>"_s,
        u"0109504000059118<GS>21SERIAL"_s,
        u"(01)09504000059118(10)LOT<GS>"_s + QChar(0x1d),
        u"(01)09504000059118(10)LOT\\x1D"_s,
    };
    for (const auto& value : invalid)
        QVERIFY2(!parseGs1(value), qPrintable(value));
    QVERIFY(parseGs1(u"(01)09504000059118(17)280229"_s));
    QVERIFY(parseGs1(u"(01)09504000059118(21)ABC(91)1234(92)ABcd+/="_s));
    QVERIFY(parseGs1(u"(00)106141411234567897(02)09504000059118(37)12"_s));
    QVERIFY(!parseGs1(QString(3000, u'A')));
}

void DocumentTest::gs1DataMatrixRoundTrip()
{
    DesignDocument document;
    auto code = makeElement(ElementType::DataMatrix, {24, 24});
    QVERIFY(code.gs1);
    QVERIFY(parseGs1(code.text));
    code.text = u"(01)09504000059118(10)LOT42(21)SERIAL-7"_s;
    document.elements = {code};
    QVERIFY(validateForExport(document));
    const QString source = toZpl(document);
    QVERIFY(source.contains(u"^BXN,3,200,0,0,1,|^FH_^FD|1010950400005911810LOT42_1D21SERIAL-7^FS"));
    QVERIFY(source.contains(u"_1D"));
    const auto parsed = QtZpl::parse(source);
    QVERIFY(parsed.has_value());
    bool foundData = false;
    for (const auto& command : parsed->labels().first().commands())
        if (const auto* data = std::get_if<QtZpl::FieldData>(&command.payload)) {
            // FNC1 must be a ^BX escape, not a leading ASCII GS. Fixed AI 01
            // has no separator; the variable lot has exactly one before 21.
            QCOMPARE(data->data, u"|1010950400005911810LOT42"_s + QChar(0x1d) + u"21SERIAL-7"_s);
            foundData = true;
        }
    QVERIFY(foundData);
    auto loaded = fromZpl(source);
    QVERIFY(loaded.has_value());
    QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
    QCOMPARE(loaded->elements.size(), 1);
    QVERIFY(loaded->elements.first().gs1);
    QCOMPARE(loaded->elements.first().text, code.text);
    loaded->originalZpl.clear();
    QCOMPARE(toZpl(*loaded), source);
    const auto preview = renderPreview(document);
    QVERIFY(preview.has_value());
    QVERIFY2(preview->diagnostics.isEmpty(), qPrintable(preview->diagnostics.join(u'\n')));
    QCOMPARE(renderPreview(*loaded)->image, preview->image);

    document.elements.first().gs1 = false;
    const auto ordinary = renderPreview(document);
    QVERIFY(ordinary.has_value());
    QVERIFY(ordinary->image != preview->image);
    document.elements.first().gs1 = true;
    document.elements.first().text = u"(01)09504000059118(10)LOT<GS>21SERIAL"_s;
    const auto punctuation = fromZpl(toZpl(document));
    QVERIFY(punctuation.has_value());
    QVERIFY2(punctuation->visualEditable, qPrintable(punctuation->importNotes.join(u'\n')));
    QCOMPARE(punctuation->elements.first().text, document.elements.first().text);
    document.elements.first().gs1 = true;
    document.elements.first().text = u"(01)09504000059119"_s;
    QVERIFY(!validateForExport(document));
    QVERIFY(!renderPreview(document));
}

void DocumentTest::editorPreviewKeepsOtherElementsAroundInvalidGs1()
{
    DesignDocument document;
    document.width = 420;
    document.height = 260;
    auto background = makeElement(ElementType::Box, {20, 20});
    background.width = 150; background.height = 80; background.thickness = 80;
    auto invalid = makeElement(ElementType::DataMatrix, {220, 32});
    invalid.text = u"NL-00001"_s;
    invalid.width = invalid.height = 96;
    auto reversed = makeElement(ElementType::Text, {30, 40});
    reversed.text = u"PART 42"_s; reversed.fontHeight = 24; reversed.reverse = true;
    auto valid = makeElement(ElementType::DataMatrix, {40, 140});
    document.elements = {background, invalid, reversed, valid};
    document.previewValues.insert(u"unused"_s, u"KEEP"_s);
    const auto originalSource = toZpl(document);
    const auto invalidId = invalid.id;
    auto withoutInvalid = document;
    withoutInvalid.elements.removeAt(1);
    const auto expected = renderPreview(withoutInvalid);
    QVERIFY2(expected.has_value(), expected ? "" : qPrintable(expected.error()));
    QVERIFY(!renderPreview(document));
    QVERIFY(!validateForExport(document));

    const auto editor = renderEditorPreview(document);
    QVERIFY2(editor.has_value(), editor ? "" : qPrintable(editor.error()));
    QCOMPARE(editor->image, expected->image);
    QCOMPARE(editor->bounds.size(), document.elements.size());
    QCOMPARE(editor->bounds[0], expected->bounds[0]);
    QCOMPARE(editor->bounds[1], QRect(220, 32, 96, 96));
    QCOMPARE(editor->bounds[2], expected->bounds[1]);
    QCOMPARE(editor->bounds[3], expected->bounds[2]);
    QCOMPARE(editor->elementErrors.keys(), QList<int>{1});
    QVERIFY(!editor->elementErrors.value(1).isEmpty());
    QVERIFY(!editor->diagnostics.isEmpty());
    QCOMPARE(document.elements[1].id, invalidId);
    QCOMPARE(document.elements[1].text, u"NL-00001"_s);
    QVERIFY(document.elements[1].gs1);
    QCOMPARE(document.elements.size(), 4);
    QCOMPARE(document.previewValues.value(u"unused"_s), u"KEEP"_s);
    QCOMPARE(toZpl(document), originalSource);
    QVERIFY(!renderPreview(document));
    QVERIFY(!validateForExport(document));
}

void DocumentTest::editorPreviewMarksUnresolvedDataMatrix()
{
    DesignDocument document;
    document.width = 400; document.height = 240;
    auto missing = makeElement(ElementType::DataMatrix, {24, 32});
    missing.text = u"(01){{gtin}}(21){{serial}}"_s;
    auto plain = makeElement(ElementType::DataMatrix, {220, 32});
    plain.gs1 = false;
    plain.text = u"[[batch]]"_s;
    document.elements = {missing, plain};
    document.previewValues.insert(u"gtin"_s, u"09506000134352"_s);
    // Unfilled valid templates remain saveable, but are never printer previews.
    QVERIFY(validateForExport(document));
    QVERIFY(!renderPreview(document));
    auto empty = document;
    empty.elements.clear();
    const auto blank = renderPreview(empty);
    QVERIFY(blank);
    const auto editor = renderEditorPreview(document);
    QVERIFY2(editor.has_value(), editor ? "" : qPrintable(editor.error()));
    QCOMPARE(editor->image, blank->image);
    QCOMPARE(editor->elementErrors.keys(), (QList<int>{0, 1}));
    QCOMPARE(editor->bounds[0], QRect(missing.x, missing.y, missing.width, missing.height));
    QCOMPARE(editor->bounds[1], QRect(plain.x, plain.y, plain.width, plain.height));
    QCOMPARE(document.elements[0].text, missing.text);
    QCOMPARE(document.elements[1].text, plain.text);
    QCOMPARE(document.previewValues.size(), 1);
    document.previewValues.insert(u"serial"_s, u"SERIAL1"_s);
    document.previewValues.insert(u"batch"_s, u"LOT1"_s);
    const auto resolved = renderPreview(document);
    const auto restored = renderEditorPreview(document);
    QVERIFY(resolved); QVERIFY(restored);
    QCOMPARE(restored->image, resolved->image);
    QVERIFY(restored->elementErrors.isEmpty());
}

void DocumentTest::editorPreviewDoesNotBypassSafetyOrSourceMode()
{
    DesignDocument document;
    auto invalid = makeElement(ElementType::DataMatrix);
    invalid.text = u"NL-00001"_s;
    document.elements = {invalid};
    document.width = 4097;
    QVERIFY(!renderEditorPreview(document));
    document.width = 400;
    document.elements[0].width = 4097;
    QVERIFY(!renderEditorPreview(document));
    document.elements[0].width = 100;
    auto otherFailure = makeElement(ElementType::Image);
    document.elements.append(otherFailure); // Missing image is not a recoverable DataMatrix field.
    QVERIFY(!renderEditorPreview(document));

    DesignDocument source;
    source.visualEditable = false;
    source.originalZpl = u"^XA^PW400^LL240^FO20,20^BXN,3,200^FD[[batch]]^FS^XZ"_s;
    const auto original = source.originalZpl;
    QVERIFY(!renderPreview(source));
    QVERIFY(!renderEditorPreview(source));
    QCOMPARE(source.originalZpl, original);
}

void DocumentTest::gs1UnsupportedImportPreservesSource()
{
    const QStringList sources{
        u"^XA^PW400^LL200^FO10,10^BXN,3,200,0,0,1,|^FD|10109504000059119^FS^XZ"_s,
        u"^XA^PW400^LL200^FO10,10^BXN,3,200,0,0,1,|^FD|101095040000591188888X^FS^XZ"_s,
        u"^XA^PW400^LL200^FO10,10^BXN,3,200,0,0,1,|^FD0109504000059118^FS^XZ"_s,
        u"^XA^PW400^LL200^FO10,10^BXN,3,200,0,0,0,|^FD|10109504000059118^FS^XZ"_s,
        u"^XA^PW400^LL200^FO10,10^BXN,3,200,0,0,1,|^FD|10109504000059118|2214001001^FS^XZ"_s,
        u"^XA^PW400^LL200^FO10,10^BXN,3,200,0,0,1,|^FD|1|10109504000059118^FS^XZ"_s,
        u"^XA^PW400^LL200^FO10,10^BXN,3,200,0,0,1,|^FD|1|d0290109504000059118^FS^XZ"_s,
    };
    for (const auto& source : sources) {
        const auto imported = fromZpl(source);
        QVERIFY2(imported.has_value(), qPrintable(source));
        QVERIFY2(!imported->visualEditable, qPrintable(source));
        QCOMPARE(toZpl(*imported), source);
        QVERIFY(imported->elements.isEmpty());
    }
    // A known GS1 field with explicit FNC1 separators imports to the same data.
    const QString supported = u"^XA^CI28^PW400^LL200^FO10,10^BXN,3,200,0,0,1,|"
        "^FD|1010950400005911810LOT|121ABC^FS^XZ"_s;
    const auto imported = fromZpl(supported);
    QVERIFY(imported.has_value());
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    QCOMPARE(imported->elements.first().text, u"(01)09504000059118(10)LOT(21)ABC"_s);
}

void DocumentTest::valueTemplateSyntaxAndResolution()
{
    const QString input = u"SN-{{serial}}/[[lot.number]]/{{serial}}/{{_key-1}}"_s;
    const auto tokens = valuePlaceholders(input);
    QVERIFY(tokens.has_value());
    QCOMPARE(tokens->size(), 4);
    QCOMPARE(tokens->first().name, u"serial"_s);
    QCOMPARE(input.mid(tokens->first().offset, tokens->first().length), u"{{serial}}"_s);
    QCOMPARE(input.mid(tokens->at(1).offset, tokens->at(1).length), u"[[lot.number]]"_s);
    const auto required = requiredValueParameters(input);
    QVERIFY(required.has_value());
    QCOMPARE(*required, QStringList({u"serial"_s, u"lot.number"_s, u"_key-1"_s}));
    const QMap<QString, QString> values{
        {u"serial"_s, u"123"_s}, {u"lot.number"_s, u"LOT-A"_s}, {u"_key-1"_s, u"END"_s},
    };
    const auto resolved = resolveValueTemplate(input, values);
    QVERIFY(resolved.has_value());
    QCOMPARE(*resolved, u"SN-123/LOT-A/123/END"_s);
    QVERIFY(!resolveValueTemplate(input, {}));
    const auto singlePass = resolveValueTemplate(u"{{first}}"_s,
        {{u"first"_s, u"[[second]]"_s}, {u"second"_s, u"MUST-NOT-EXPAND"_s}});
    QVERIFY(singlePass.has_value());
    QCOMPARE(*singlePass, u"[[second]]"_s);
    QCOMPARE(*resolveValueTemplate(u"A{{empty}}B"_s, {{u"empty"_s, QString()}}), u"AB"_s);
    QCOMPARE(*resolveValueTemplate(u"literal"_s, {}), u"literal"_s);
    for (const auto& invalid : {u"{{serial}"_s, u"[[serial]"_s, u"{{}}"_s, u"[[]]"_s,
            u"{{1name}}"_s, u"{{имя}}"_s, u"{{a b}}"_s, u"{{a{{b}}}}"_s,
            u"value}}"_s, u"value]]"_s, u"[[serial}}"_s})
        QVERIFY2(!valuePlaceholders(invalid), qPrintable(invalid));
}

void DocumentTest::gs1TemplatesValidateKnownData()
{
    const QString input = u"(01){{gtin}}(17)271231(10)[[lot]](21)SN-{{serial}}"_s;
    const auto parsed = parseGs1Template(input);
    QVERIFY2(parsed.has_value(), qPrintable(parsed ? QString{} : parsed.error()));
    QCOMPARE(parsed->canonicalText, input);
    QCOMPARE(parsed->fields.size(), 4);
    QVERIFY(!parsed->wholeCodePlaceholder);
    QCOMPARE(parsed->requiredParameters, QStringList({u"gtin"_s, u"lot"_s, u"serial"_s}));
    QCOMPARE(parsed->elementString, u"01{{gtin}}1727123110[[lot]]"_s + QChar(0x1d) + u"21SN-{{serial}}"_s);
    const auto rebuilt = buildGs1Template(parsed->fields);
    QVERIFY(rebuilt.has_value());
    QCOMPARE(rebuilt->canonicalText, input);
    QVERIFY(!parseGs1(input));
    for (const auto& whole : {u"{{datamatrix}}"_s, u"[[full.code]]"_s}) {
        const auto full = parseGs1Template(whole);
        QVERIFY(full.has_value());
        QVERIFY(full->wholeCodePlaceholder);
        QVERIFY(full->fields.isEmpty());
        QCOMPARE(full->canonicalText, whole);
        QCOMPARE(full->elementString, whole);
    }
    const QStringList invalid{
        u"(01)09504000059119(21){{serial}}"_s,
        u"(01){{gtin}}(17)271331"_s,
        u"(01)A{{gtin}}"_s,
        u"(01)123456789012345{{gtin}}"_s,
        u"(01){{gtin}}(21)SN {{serial}}"_s,
        u"(01){{gtin}}(21){{serial}"_s,
        u"(01){{gtin}}(9999){{unknown}}"_s,
        u"({{ai}})1234"_s,
        u"(01){{gtin}}(21)"_s,
        u"(01){{gtin}}(21){{serial}}(21)DUPLICATE"_s,
        u"01PREFIX{{gtin}}21{{serial}}"_s,
        u"{{gtin}}{{serial}}"_s,
        u"prefix-{{datamatrix}}"_s,
    };
    for (const auto& value : invalid)
        QVERIFY2(!parseGs1Template(value), qPrintable(value));
    const auto concrete = parseGs1Template(u"(01)09504000059118(21)ABC"_s);
    QVERIFY(concrete.has_value());
    QVERIFY(concrete->requiredParameters.isEmpty());
}

void DocumentTest::gs1TemplateResolutionPreservesFieldBoundaries()
{
    const QString input = u"(01){{gtin}}(10)[[lot]](21)SN-{{serial}}"_s;
    QMap<QString, QString> values{
        {u"gtin"_s, u"09504000059118"_s}, {u"lot"_s, u"LOT(17)"_s}, {u"serial"_s, u"123"_s},
    };
    const auto resolved = resolveGs1Template(input, values);
    QVERIFY2(resolved.has_value(), qPrintable(resolved ? QString{} : resolved.error()));
    QVERIFY(resolved->requiredParameters.isEmpty());
    QCOMPARE(resolved->fields.size(), 3);
    QCOMPARE(resolved->fields[1].value, u"LOT(17)"_s);
    QCOMPARE(resolved->fields[2].value, u"SN-123"_s);
    QCOMPARE(resolved->canonicalText, u"(01)09504000059118(10)LOT\\(17\\)(21)SN-123"_s);
    QCOMPARE(parseGs1(resolved->canonicalText)->fields, resolved->fields);
    QVERIFY(!resolveGs1Template(input, {}));
    values[u"gtin"_s] = u"09504000059119"_s;
    QVERIFY(!resolveGs1Template(input, values));
    values[u"gtin"_s] = u"09504000059118"_s;
    values[u"lot"_s] = u"LOT"_s + QChar(0x1d) + u"21INJECTED"_s;
    QVERIFY(!resolveGs1Template(input, values));
    values[u"lot"_s] = u"{{another}}"_s;
    values[u"another"_s] = u"NEVER-RECURSE"_s;
    QVERIFY(!resolveGs1Template(input, values));
    values[u"lot"_s] = u"LOT"_s;
    values[u"serial"_s] = QString(30, u'A');
    QVERIFY(!resolveGs1Template(input, values));

    const auto full = resolveGs1Template(u"{{datamatrix}}"_s,
        {{u"datamatrix"_s, u"]d2010950400005911810LOT<GS>21SERIAL"_s}});
    QVERIFY2(full.has_value(), qPrintable(full ? QString{} : full.error()));
    QCOMPARE(full->canonicalText, u"(01)09504000059118(10)LOT(21)SERIAL"_s);
    QVERIFY(!full->wholeCodePlaceholder);
    QVERIFY(full->requiredParameters.isEmpty());
    QVERIFY(!resolveGs1Template(u"[[full]]"_s, {{u"full"_s, u"{{nested}}"_s}}));
}

void DocumentTest::markerPlaceholdersKeepNamespacesAndBuiltins()
{
    const QString input = u"{{01}}|{{21}}|{{gS}}|[[ORDER_NAME]]|[[weight]]|{{11}}|[[11]]|{{17}}|[[17]]|[[01]]|{{legacyCase}}"_s;
    const auto tokens = valuePlaceholders(input);
    QVERIFY(tokens);
    QCOMPARE(tokens->at(0).kind, ValuePlaceholderKind::NumericField);
    QCOMPARE(tokens->at(2).kind, ValuePlaceholderKind::GroupSeparator);
    QCOMPARE(tokens->at(3).kind, ValuePlaceholderKind::AdditionalField);
    QCOMPARE(tokens->last().kind, ValuePlaceholderKind::Named);
    QCOMPARE(tokens->at(3).name, u"order_name"_s);
    QCOMPARE(tokens->at(6).name, u"[[11]]"_s);
    QCOMPARE(tokens->at(8).name, u"[[17]]"_s);
    const auto required = requiredValueParameters(input);
    QVERIFY(required);
    QCOMPARE(*required, QStringList({u"01"_s, u"21"_s, u"order_name"_s, u"weight"_s,
        u"11"_s, u"[[11]]"_s, u"17"_s, u"[[17]]"_s, u"legacyCase"_s}));
    const QMap<QString, QString> values{
        {u"01"_s, u"09504000059118"_s}, {u"21"_s, u"SERIAL"_s},
        {u"order_name"_s, u"Order"_s}, {u"weight"_s, u"12.5"_s},
        {u"11"_s, u"261007"_s}, {u"[[11]]"_s, u"07.10.2026"_s},
        {u"17"_s, u"271007"_s}, {u"[[17]]"_s, u"2027/10/07"_s},
        {u"legacyCase"_s, u"legacy"_s}, {u"GS"_s, u"MUST-NOT-OVERRIDE"_s},
    };
    const auto resolved = resolveValueTemplate(input, values);
    QVERIFY(resolved);
    QCOMPARE(*resolved, u"09504000059118|SERIAL|"_s + QChar(0x1d)
        + u"|Order|12.5|261007|07.10.2026|271007|2027/10/07|09504000059118|legacy"_s);
    QVERIFY(requiredValueParameters(u"{{GS}}{{gs}}{{Gs}}")->isEmpty());
    QCOMPARE(*resolveValueTemplate(u"{{GS}}{{gs}}{{Gs}}", {}), QString(3, QChar(0x1d)));
    QVERIFY(!resolveValueTemplate(u"{{11}}|[[11]]", {{u"11"_s, u"261007"_s}}));
    QCOMPARE(*resolveValueTemplate(u"[[BATCH]]|[[batch]]", {{u"batch"_s, u"A"_s}}), u"A|A"_s);
    QVERIFY(valuePlaceholders(u"[[123field]]"));
    QVERIFY(!valuePlaceholders(u"{{123}}"));
    QCOMPARE(*resolveValueTemplate(u"{{21}}|{{01}}", {{u"21"_s, u"{{01}}"_s}, {u"01"_s, u"GTIN"_s}}),
        u"{{01}}|GTIN"_s);
}

void DocumentTest::rawGs1MarkerTemplatesPreserveAndResolve()
{
    const QString text = u"01{{01}}21{{21}}{{gS}}91{{91}}{{GS}}92{{92}}"_s;
    const auto parsed = parseGs1Template(text);
    QVERIFY2(parsed.has_value(), qPrintable(parsed ? QString{} : parsed.error()));
    QCOMPARE(parsed->canonicalText, text);
    QCOMPARE(parsed->elementString, text);
    QCOMPARE(parsed->fields.size(), 4);
    QCOMPARE(parsed->fields[1], (Gs1Field{u"21"_s, u"{{21}}"_s}));
    QCOMPARE(parsed->requiredParameters, QStringList({u"01"_s, u"21"_s, u"91"_s, u"92"_s}));
    const QMap<QString, QString> values{
        {u"01"_s, u"09504000059118"_s}, {u"21"_s, u"SER(10)"_s},
        {u"91"_s, u"ABCD"_s}, {u"92"_s, u"abc123+/="_s},
    };
    const auto resolved = resolveGs1Template(text, values);
    QVERIFY2(resolved.has_value(), qPrintable(resolved ? QString{} : resolved.error()));
    QCOMPARE(resolved->fields[1].value, u"SER(10)"_s);
    QCOMPARE(resolved->elementString, u"010950400005911821SER(10)"_s + QChar(0x1d)
        + u"91ABCD"_s + QChar(0x1d) + u"92abc123+/="_s);
    QVERIFY(!resolveGs1Template(text, {}));
    QVERIFY(parseGs1Template(u"01{{gtin}}21SN-[[SERIAL]]"));
    const auto simple = parseGs1Template(u"(01){{01}}(21){{21}}(91){{91}}"_s);
    QVERIFY(simple);
    QCOMPARE(simple->elementString, u"01{{01}}21{{21}}"_s + QChar(0x1d) + u"91{{91}}"_s);
    const QString gsOnlyText = u"010950400005911821SER{{GS}}91ABCD"_s;
    const auto gsOnly = parseGs1Template(gsOnlyText);
    QVERIFY(gsOnly);
    QVERIFY(gsOnly->requiredParameters.isEmpty());
    const auto gsOnlyResolved = resolveGs1Template(gsOnlyText, {});
    QVERIFY(gsOnlyResolved);
    QCOMPARE(gsOnlyResolved->elementString, u"010950400005911821SER"_s + QChar(0x1d) + u"91ABCD"_s);
    for (const auto& invalid : {u"01{{01}}21{{21}}91{{91}}"_s,
            u"01ABC{{01}}21{{21}}"_s, u"01{{01}}{{GS}}21{{21}}"_s,
            u"01{{01}}21{{21}}{{GS}}"_s, u"{{GS}}"_s,
            u"(01){{01}}(21){{21}}{{GS}}(91){{91}}"_s})
        QVERIFY2(!parseGs1Template(invalid), qPrintable(invalid));
}

void DocumentTest::exportMustRemainReopenable()
{
    auto document = sampleDocument();
    QVERIFY(validateForExport(document).has_value());
    document.elements = QList<Element>(201, makeElement(ElementType::Box));
    QVERIFY(!validateForExport(document));

    document = {};
    Element image = makeElement(ElementType::Image);
    image.width = image.height = 4096;
    image.image = QImage(4096, 4096, QImage::Format_Grayscale8);
    image.image.fill(Qt::black);
    document.elements = {image, image};
    QVERIFY(!validateForExport(document));

    document = {};
    document.originalZpl = u"^XA^FX"_s + QString(4 * 1024 * 1024, u'Я') + u"^XZ"_s;
    QVERIFY(document.originalZpl.size() < 8 * 1024 * 1024);
    QVERIFY(!validateForExport(document));
    QVERIFY(!fromZpl(document.originalZpl));

    const QString original = u"^XA^PW200^LL100^FXPreserve note^FS^FO5,5^GB20,20,2^FS^XZ\r\n"_s;
    auto imported = fromZpl(original);
    QVERIFY(imported.has_value());
    QVERIFY(imported->visualEditable);
    QVERIFY(validateForExport(*imported).has_value());
    QCOMPARE(toZpl(*imported), original);
}

void DocumentTest::resizedElementsRemainPrintableAndReopenable()
{
    for (const auto type : {ElementType::Text, ElementType::Box, ElementType::Line,
             ElementType::Ellipse, ElementType::Circle, ElementType::Code128,
             ElementType::QrCode, ElementType::DataMatrix, ElementType::Image}) {
        DesignDocument document;
        document.width = document.height = 600;
        auto original = makeElement(type, {20, 20});
        original.gs1 = false;
        original.text = u"AB12"_s;
        original.width = 96;
        original.height = type == ElementType::Circle ? 96 : 48;
        original.moduleWidth = 2;
        if (type == ElementType::Image) {
            original.image = QImage(12, 6, QImage::Format_ARGB32);
            original.image.fill(Qt::black);
        }
        document.elements = {original};
        const auto before = renderPreview(document);
        QVERIFY2(before.has_value(), qPrintable(elementTypeName(type)));
        const QSize current = before->bounds.first().size();
        const Element resized = resizedElement(original, current * 2, current);
        QCOMPARE(resized.id, original.id);
        QCOMPARE(resized.x, original.x);
        QCOMPARE(resized.y, original.y);
        QCOMPARE(resized.text, original.text);
        QCOMPARE(resized.image, original.image);
        document.elements = {resized};
        QVERIFY(validateForExport(document).has_value());
        const auto after = renderPreview(document);
        QVERIFY(after.has_value());
        QVERIFY2(after->bounds.first().width() > current.width(), qPrintable(elementTypeName(type)));
        QVERIFY2(after->bounds.first().height() > current.height(), qPrintable(elementTypeName(type)));
        const QString source = toZpl(document);
        auto loaded = fromZpl(source);
        QVERIFY(loaded.has_value());
        QVERIFY2(loaded->visualEditable, qPrintable(loaded->importNotes.join(u'\n')));
        loaded->originalZpl.clear();
        const auto reopened = renderPreview(*loaded);
        QVERIFY(reopened.has_value());
        QCOMPARE(reopened->image, after->image);
    }
}

void DocumentTest::textResizeRespectsOrientationAndAutomaticWidth()
{
    for (const QChar orientation : QStringView{u"NRIB"}) {
        DesignDocument document;
        document.width = document.height = 500;
        Element original = makeElement(ElementType::Text, {20, 20});
        original.text = u"Resize"_s;
        original.fontHeight = 40;
        original.orientation = orientation;
        document.elements = {original};
        const auto before = renderPreview(document);
        QVERIFY(before.has_value());
        const QSize current = before->bounds.first().size();
        const bool rotated = orientation == u'R' || orientation == u'B';
        QSize requested = current;
        if (rotated) requested.setHeight(current.height() * 2);
        else requested.setWidth(current.width() * 2);
        const auto wider = resizedElement(original, requested, current);
        QCOMPARE(wider.fontHeight, original.fontHeight);
        QCOMPARE(wider.fontWidth, original.fontHeight * 2);
        document.elements = {wider};
        const auto widePreview = renderPreview(document);
        QVERIFY(widePreview.has_value());
        QSize wideSize = widePreview->bounds.first().size();
        QSize localCurrent = current;
        if (rotated) {
            wideSize.transpose();
            localCurrent.transpose();
        }
        QVERIFY(wideSize.width() > localCurrent.width() * 1.8);
        QCOMPARE(wideSize.height(), localCurrent.height());

        requested = current;
        if (rotated) requested.setWidth(current.width() * 2);
        else requested.setHeight(current.height() * 2);
        const auto taller = resizedElement(original, requested, current);
        QVERIFY(taller.fontHeight > original.fontHeight);
        QVERIFY(taller.fontWidth > 0); // Height alone must not widen automatic text.
        document.elements = {taller};
        const auto tallPreview = renderPreview(document);
        QVERIFY(tallPreview.has_value());
        QSize tallSize = tallPreview->bounds.first().size();
        if (rotated) tallSize.transpose();
        QVERIFY(tallSize.height() > localCurrent.height() * 1.8);
        QVERIFY(qAbs(tallSize.width() - localCurrent.width()) <= 6);

        const auto uniform = resizedElement(original, current * 2, current);
        QCOMPARE(uniform.fontWidth, 0);
        const auto unchanged = resizedElement(original, current, current);
        QCOMPARE(elementZpl(unchanged), elementZpl(original));
    }

    // Explicit printer widths are independent of height and have no artificial
    // QFont stretch ceiling. A wide field must still resize proportionally.
    DesignDocument document;
    document.width = 600;
    auto stretched = makeElement(ElementType::Text, {20, 20});
    stretched.text = u"AB"_s;
    stretched.fontHeight = 40;
    stretched.fontWidth = 512;
    document.elements = {stretched};
    const auto before = renderPreview(document);
    QVERIFY(before.has_value());
    const QSize current = before->bounds.first().size();
    const auto narrower = resizedElement(stretched, {current.width() / 2, current.height()}, current);
    QVERIFY(qAbs(narrower.fontWidth - 256) <= 1);
    QCOMPARE(narrower.fontHeight, 40);
    document.elements = {narrower};
    const auto after = renderPreview(document);
    QVERIFY(after.has_value());
    QVERIFY(after->bounds.first().width() < current.width() * 0.6);
    QVERIFY(after->bounds.first().width() > current.width() * 0.4);
}

void DocumentTest::textBlockResizePreservesIndependentAxes()
{
    for (const QChar orientation : QStringView{u"NRIB"}) {
        DesignDocument document;
        document.width = document.height = 800;
        auto original = makeElement(ElementType::Text, {40, 40});
        original.text = u"Milk"_s;
        original.fontHeight = 40;
        original.fontWidth = 30;
        original.blockWidth = 200;
        original.blockMaxLines = 2;
        original.blockLineSpacing = 4;
        original.blockHangingIndent = 12;
        original.blockJustification = u'C';
        original.orientation = orientation;
        document.elements = {original};
        const auto before = renderPreview(document);
        QVERIFY(before.has_value());
        QSize logical = before->bounds.first().size();
        const bool rotated = orientation == u'R' || orientation == u'B';
        if (rotated) logical.transpose();
        QSize requested(logical.width() * 2, qRound(logical.height() * 1.5));
        if (rotated) requested.transpose();
        const auto resized = resizedElement(original, requested, before->bounds.first().size());
        QCOMPARE(resized.fontWidth, 60);
        QCOMPARE(resized.fontHeight, 60);
        QCOMPARE(resized.blockWidth, 400);
        QCOMPARE(resized.blockMaxLines, 2);
        QCOMPARE(resized.blockLineSpacing, 6);
        QCOMPARE(resized.blockHangingIndent, 24);
        QCOMPARE(resized.blockJustification, u'C');
        document.elements = {resized};
        const auto after = renderPreview(document);
        QVERIFY(after.has_value());
        const auto actual = after->bounds.first().size();
        QVERIFY(qAbs(actual.width() - requested.width()) <= 3);
        QVERIFY(qAbs(actual.height() - requested.height()) <= 3);
        const auto reopened = fromZpl(toZpl(document));
        QVERIFY(reopened.has_value());
        QVERIFY(reopened->visualEditable);
        QCOMPARE(reopened->elements.first().blockWidth, 400);
        QCOMPARE(reopened->elements.first().fontWidth, 60);
    }
}

void DocumentTest::barcodeResizeKeepsCaptionAndModules()
{
    for (const QChar orientation : QStringView{u"NRIB"}) {
        for (const bool caption : {false, true}) {
            DesignDocument document;
            document.width = document.height = 600;
            // ^FO anchors the bar body; the rotated caption can extend left
            // or above it. Keep the complete symbol inside the label so this
            // test measures resizing rather than clipping at the label edge.
            Element original = makeElement(ElementType::Code128, {100, 100});
            original.text = u"123456"_s;
            original.moduleWidth = 2;
            original.height = 60;
            original.orientation = orientation;
            original.humanReadable = caption;
            document.elements = {original};
            const auto before = renderPreview(document);
            QVERIFY(before.has_value());
            QVERIFY(before->image.rect().contains(before->bounds.first().adjusted(-1, -1, 1, 1)));
            const QSize current = before->bounds.first().size();
            const bool rotated = orientation == u'R' || orientation == u'B';
            QSize requested = current;
            if (rotated) requested.rwidth() += 35;
            else requested.rheight() += 35;
            const auto taller = resizedElement(original, requested, current);
            QCOMPARE(taller.height, original.height + 35);
            QCOMPARE(taller.moduleWidth, original.moduleWidth);
            QCOMPARE(taller.humanReadable, caption);
            document.elements = {taller};
            const auto after = renderPreview(document);
            QVERIFY(after.has_value());
            QCOMPARE(after->bounds.first().size(), requested);

            requested = current;
            if (rotated) requested.setHeight(current.height() * 2);
            else requested.setWidth(current.width() * 2);
            const auto wider = resizedElement(original, requested, current);
            QCOMPARE(wider.moduleWidth, original.moduleWidth * 2);
            if (caption) QVERIFY(wider.height < original.height);
            else QCOMPARE(wider.height, original.height);
            document.elements = {wider};
            const auto widerPreview = renderPreview(document);
            QVERIFY(widerPreview.has_value());
            QVERIFY(widerPreview->image.rect().contains(widerPreview->bounds.first().adjusted(-1, -1, 1, 1)));
            auto wideBounds = widerPreview->bounds.first().size();
            if (rotated) { wideBounds.transpose(); requested.transpose(); }
            QVERIFY(qAbs(wideBounds.height() - requested.height()) <= 3);
        }
    }
}

void DocumentTest::resizeLimitsAndUniformAxes()
{
    for (const auto type : {ElementType::Box, ElementType::Line, ElementType::Ellipse, ElementType::Image}) {
        auto element = makeElement(type);
        element.imageKeepAspectRatio = false;
        const auto resized = resizedElement(element, {-100, 9000}, {120, 80});
        QCOMPARE(resized.width, 1);
        QCOMPARE(resized.height, 4096);
    }
    auto image = makeElement(ElementType::Image);
    image.width = 200; image.height = 100;
    const auto largeImage = resizedElement(image, {9000, 100}, {200, 100});
    QCOMPARE(largeImage.width, 4096);
    QCOMPARE(largeImage.height, 2048);
    const auto smallImage = resizedElement(image, {-100, 100}, {200, 100});
    QCOMPARE(smallImage.width, 1);
    QCOMPARE(smallImage.height, 1);
    auto circle = makeElement(ElementType::Circle);
    const auto tallerCircle = resizedElement(circle, {100, 150}, {100, 100});
    QCOMPARE(tallerCircle.width, 150);
    QCOMPARE(tallerCircle.height, 150);
    const auto smallerCircle = resizedElement(circle, {60, 100}, {100, 100});
    QCOMPARE(smallerCircle.width, 60);
    QCOMPARE(smallerCircle.height, 60);
    for (const auto type : {ElementType::QrCode, ElementType::DataMatrix}) {
        const auto code = makeElement(type);
        QCOMPARE(resizedElement(code, {100, 200}, {100, 100}).moduleWidth, 6);
        QCOMPARE(resizedElement(code, {200, 100}, {100, 100}).moduleWidth, 6);
        QCOMPARE(resizedElement(code, {1, 1}, {100, 100}).moduleWidth, 1);
        QCOMPARE(resizedElement(code, {4096, 4096}, {100, 100}).moduleWidth, 10);
        QCOMPARE(resizedElement(code, {101, 100}, {100, 100}).moduleWidth, 3);
    }
    auto code = makeElement(ElementType::Code128);
    const auto largeCode = resizedElement(code, {4096, 4096}, {100, 100});
    QCOMPARE(largeCode.moduleWidth, 10);
    QCOMPARE(largeCode.height, 1024);
    const auto smallCode = resizedElement(code, {1, 1}, {100, 100});
    QCOMPARE(smallCode.moduleWidth, 1);
    QCOMPARE(smallCode.height, 1);
    const auto text = makeElement(ElementType::Text);
    const auto largeText = resizedElement(text, {4096, 4000}, {20, 20});
    QCOMPARE(largeText.fontHeight, 512);
    QCOMPARE(largeText.fontWidth, 512);
    const auto fallback = resizedElement(text, {200, 80}, {});
    QVERIFY(fallback.fontHeight >= 1 && fallback.fontHeight <= 512);
    QVERIFY(fallback.fontWidth >= 0 && fallback.fontWidth <= 512);
}

QTEST_MAIN(DocumentTest)
#include "test_document.moc"
