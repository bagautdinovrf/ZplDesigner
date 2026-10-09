#include "design_document.hpp"

#include <QtTest>
#include <algorithm>
#include <array>

using namespace Designer;

class ImageConverterTests final : public QObject {
    Q_OBJECT
private slots:
    void thresholdKeepsBoundaryAndWhiteAlphaBackground();
    void formatsPreserveThresholdAndDither_data();
    void formatsPreserveThresholdAndDither();
    void grayRowsRespectStrideAndManualThreshold();
    void ditherPreservesKnownPattern();
    void sampleGraphicsSurviveZpl();
};

void ImageConverterTests::thresholdKeepsBoundaryAndWhiteAlphaBackground()
{
    auto element = makeElement(ElementType::Image);
    element.image = QImage(9, 1, QImage::Format_ARGB32);
    const std::array pixels{
        qRgba(0, 0, 0, 0), qRgba(0, 0, 0, 127), qRgba(0, 0, 0, 128),
        qRgba(127, 127, 127, 255), qRgba(128, 128, 128, 255),
        qRgba(255, 255, 255, 255), qRgba(0, 0, 0, 255),
        qRgba(1, 1, 1, 128), qRgba(2, 2, 2, 128)};
    for (int x = 0; x < int(pixels.size()); ++x) element.image.setPixel(x, 0, pixels[x]);
    element.width = 9; element.height = 1;
    const QImage original = element.image.copy();
    // Black alpha 127 composites to exactly 128 (white at the boundary).
    // Gray 1/alpha 128 composites to 127.50; gray 2 to 128.00+.
    const std::array<int, 9> expected{255, 255, 0, 0, 255, 255, 0, 0, 255};
    for (const bool reverse : {false, true}) {
        element.reverse = reverse;
        const auto result = monochromeImage(element);
        QCOMPARE(result.size(), QSize(9, 1));
        for (int x = 0; x < int(expected.size()); ++x)
            QCOMPARE(result.pixelColor(x, 0).red(), reverse ? 255 - expected[x] : expected[x]);
    }
    QCOMPARE(element.image, original);
}

void ImageConverterTests::formatsPreserveThresholdAndDither_data()
{
    QTest::addColumn<int>("format");
    for (const auto format : {QImage::Format_Grayscale8, QImage::Format_RGB32,
             QImage::Format_ARGB32, QImage::Format_ARGB32_Premultiplied,
             QImage::Format_RGBA8888, QImage::Format_RGBA8888_Premultiplied,
             QImage::Format_RGB888, QImage::Format_Indexed8, QImage::Format_Mono})
        QTest::newRow(qPrintable(QString::number(format))) << int(format);
}

void ImageConverterTests::formatsPreserveThresholdAndDither()
{
    QFETCH(int, format);
    QImage image(19, 7, QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgba((x * 37 + y * 11) % 256, (x * 13 + y * 41) % 256,
                                      (x * 29 + y * 17) % 256, (x * 53 + y * 31) % 256));
    auto element = makeElement(ElementType::Image);
    element.image = image.convertToFormat(static_cast<QImage::Format>(format));
    for (const QSize size : {QSize(19, 7), QSize(11, 3), QSize(31, 13)}) {
        element.width = size.width(); element.height = size.height();
        // Compare against the same Qt-scaled pixels in the canonical straight-alpha
        // format. This checks dispatch/stride/alpha without changing Qt's scaler.
        auto canonical = element;
        canonical.image = element.image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_ARGB32);
        for (const bool dither : {false, true}) {
            element.dither = canonical.dither = dither;
            for (const int threshold : {0, 1, 87, 127, 128, 254, 255}) {
                element.threshold = canonical.threshold = threshold;
                for (const bool reverse : {false, true}) {
                    element.reverse = canonical.reverse = reverse;
                    QCOMPARE(monochromeImage(element), monochromeImage(canonical));
                }
            }
        }
    }
}

void ImageConverterTests::grayRowsRespectStrideAndManualThreshold()
{
    auto element = makeElement(ElementType::Image);
    element.image = QImage(7, 3, QImage::Format_Grayscale8);
    element.image.fill(255);
    const std::array<uchar, 7> values{0, 1, 86, 87, 127, 128, 255};
    for (int y = 0; y < 3; ++y)
        std::copy(values.begin(), values.end(), element.image.scanLine(y));
    element.width = 7; element.height = 3;
    element.threshold = 87;
    const auto result = monochromeImage(element);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 7; ++x)
            QCOMPARE(result.pixelColor(x, y), QColor(x < 3 ? Qt::black : Qt::white));
    element.threshold = -1;
    const auto white = monochromeImage(element);
    element.threshold = 256;
    const auto maximum = monochromeImage(element);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 7; ++x) {
            QCOMPARE(white.pixelColor(x, y), QColor(Qt::white));
            QCOMPARE(maximum.pixelColor(x, y), QColor(x == 6 ? Qt::white : Qt::black));
        }
}

void ImageConverterTests::ditherPreservesKnownPattern()
{
    auto element = makeElement(ElementType::Image);
    element.width = 7; element.height = 4;
    element.image = QImage(element.width, element.height, QImage::Format_Grayscale8);
    element.image.fill(128);
    element.dither = true;
    for (const bool reverse : {false, true}) {
        element.reverse = reverse;
        const auto result = monochromeImage(element);
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x) {
                const bool white = ((x + y) % 2 == 0) != reverse;
                QCOMPARE(result.pixelColor(x, y), QColor(white ? Qt::white : Qt::black));
            }
    }
}

void ImageConverterTests::sampleGraphicsSurviveZpl()
{
    const auto sample = sampleDocument();
    int images = 0;
    for (auto element : sample.elements) {
        if (element.type != ElementType::Image) continue;
        ++images;
        element.x = element.y = 0;
        const QImage bitmap = monochromeImage(element);
        QVERIFY(!bitmap.isNull());
        DesignDocument single;
        single.width = (element.width + 7) / 8 * 8;
        single.height = element.height;
        single.elements = {element};
        const auto preview = renderPreview(single);
        QVERIFY(preview);
        QVERIFY(preview->diagnostics.isEmpty());
        QCOMPARE(preview->image.copy(bitmap.rect()).convertToFormat(QImage::Format_Grayscale8), bitmap);
        for (int y = 0; y < single.height; ++y)
            for (int x = bitmap.width(); x < single.width; ++x)
                QCOMPARE(preview->image.pixelColor(x, y), QColor(Qt::white));
    }
    QCOMPARE(images, 2);
}

QTEST_MAIN(ImageConverterTests)
#include "test_image_converter.moc"
