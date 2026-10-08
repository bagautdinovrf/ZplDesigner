#include "image_operations.hpp"

#include <QtTest>

using Designer::Element;
using Designer::ElementType;

class ImageOperationsTests final : public QObject {
    Q_OBJECT

private slots:
    void cropPreservesExactPixelsAndScalesPrintSize();
    void cropKeepsAtLeastOnePrinterDot();
    void rotations_data();
    void rotations();
    void mirrors_data();
    void mirrors();
    void transformationsAreLosslessAndKeepTheOriginal();
    void rejectsInvalidImagesAndCropBounds();

private:
    [[nodiscard]] static Element imageElement();
};

Element ImageOperationsTests::imageElement()
{
    Element element = Designer::makeElement(ElementType::Image, {57, 83});
    element.name = QStringLiteral("Original picture");
    element.width = 50;
    element.height = 30;
    element.threshold = 97;
    element.dither = true;
    element.reverse = true;
    element.image = QImage(5, 3, QImage::Format_ARGB32);
    for (int y = 0; y < element.image.height(); ++y) {
        for (int x = 0; x < element.image.width(); ++x)
            element.image.setPixel(x, y, qRgba(x * 41 + y * 3, y * 75 + x, x + y * 13, 100 + x * 19 + y * 9));
    }
    return element;
}

void ImageOperationsTests::cropPreservesExactPixelsAndScalesPrintSize()
{
    const Element original = imageElement();
    const QRect bounds(1, 1, 3, 2);
    const auto cropped = Designer::cropImageElement(original, bounds);
    QVERIFY(cropped);
    QCOMPARE(cropped->image.size(), QSize(3, 2));
    QCOMPARE(cropped->width, 30);
    QCOMPARE(cropped->height, 20);
    QCOMPARE(cropped->id, original.id);
    QCOMPARE(cropped->name, original.name);
    QCOMPARE(cropped->x, original.x);
    QCOMPARE(cropped->y, original.y);
    QCOMPARE(cropped->threshold, original.threshold);
    QCOMPARE(cropped->dither, original.dither);
    QCOMPARE(cropped->reverse, original.reverse);
    for (int y = 0; y < bounds.height(); ++y) {
        for (int x = 0; x < bounds.width(); ++x)
            QCOMPARE(cropped->image.pixel(x, y), original.image.pixel(x + bounds.x(), y + bounds.y()));
    }
    QCOMPARE(original.image.size(), QSize(5, 3));
    QCOMPARE(original.width, 50);
    QCOMPARE(original.height, 30);
}

void ImageOperationsTests::cropKeepsAtLeastOnePrinterDot()
{
    auto original = imageElement();
    original.width = original.height = 1;
    const auto cropped = Designer::cropImageElement(original, QRect(4, 2, 1, 1));
    QVERIFY(cropped);
    QCOMPARE(cropped->width, 1);
    QCOMPARE(cropped->height, 1);
    QCOMPARE(cropped->image.pixel(0, 0), original.image.pixel(4, 2));
}

void ImageOperationsTests::rotations_data()
{
    QTest::addColumn<int>("angle");
    QTest::newRow("unchanged") << 0;
    QTest::newRow("clockwise") << 90;
    QTest::newRow("half-turn") << 180;
    QTest::newRow("counter-clockwise") << 270;
}

void ImageOperationsTests::rotations()
{
    QFETCH(int, angle);
    const Element original = imageElement();
    const auto rotated = Designer::rotateImageElement(original, angle);
    QVERIFY(rotated);
    const bool swapped = angle == 90 || angle == 270;
    QCOMPARE(rotated->image.size(), swapped ? QSize(3, 5) : QSize(5, 3));
    QCOMPARE(rotated->width, swapped ? 30 : 50);
    QCOMPARE(rotated->height, swapped ? 50 : 30);
    QCOMPARE(rotated->id, original.id);
    QCOMPARE(rotated->x, original.x);
    QCOMPARE(rotated->y, original.y);
    for (int y = 0; y < original.image.height(); ++y) {
        for (int x = 0; x < original.image.width(); ++x) {
            QPoint destination(x, y);
            if (angle == 90) destination = {original.image.height() - 1 - y, x};
            else if (angle == 180) destination = {original.image.width() - 1 - x, original.image.height() - 1 - y};
            else if (angle == 270) destination = {y, original.image.width() - 1 - x};
            QCOMPARE(rotated->image.pixel(destination), original.image.pixel(x, y));
        }
    }
}

void ImageOperationsTests::mirrors_data()
{
    QTest::addColumn<Qt::Orientation>("axis");
    QTest::newRow("horizontal") << Qt::Horizontal;
    QTest::newRow("vertical") << Qt::Vertical;
}

void ImageOperationsTests::mirrors()
{
    QFETCH(Qt::Orientation, axis);
    const Element original = imageElement();
    const auto mirrored = Designer::mirrorImageElement(original, axis);
    QVERIFY(mirrored);
    QCOMPARE(mirrored->image.size(), original.image.size());
    QCOMPARE(mirrored->width, original.width);
    QCOMPARE(mirrored->height, original.height);
    QCOMPARE(mirrored->id, original.id);
    QCOMPARE(mirrored->x, original.x);
    QCOMPARE(mirrored->y, original.y);
    for (int y = 0; y < original.image.height(); ++y) {
        for (int x = 0; x < original.image.width(); ++x) {
            const QPoint destination(axis == Qt::Horizontal ? original.image.width() - 1 - x : x,
                                     axis == Qt::Vertical ? original.image.height() - 1 - y : y);
            QCOMPARE(mirrored->image.pixel(destination), original.image.pixel(x, y));
        }
    }
}

void ImageOperationsTests::transformationsAreLosslessAndKeepTheOriginal()
{
    const Element original = imageElement();
    auto working = original;
    for (int i = 0; i < 4; ++i) {
        const auto rotated = Designer::rotateImageElement(working, 90);
        QVERIFY(rotated);
        working = *rotated;
    }
    QCOMPARE(working.image, original.image);
    QCOMPARE(working.width, original.width);
    QCOMPARE(working.height, original.height);
    for (const auto axis : {Qt::Horizontal, Qt::Vertical}) {
        for (int i = 0; i < 2; ++i) {
            const auto mirrored = Designer::mirrorImageElement(working, axis);
            QVERIFY(mirrored);
            working = *mirrored;
        }
        QCOMPARE(working.image, original.image);
    }
    const auto unchanged = Designer::cropImageElement(original, original.image.rect());
    QVERIFY(unchanged);
    QCOMPARE(unchanged->image, original.image);
    QCOMPARE(original.image, imageElement().image);
}

void ImageOperationsTests::rejectsInvalidImagesAndCropBounds()
{
    const Element original = imageElement();
    for (const QRect bounds : {QRect(), QRect(-1, 0, 2, 2), QRect(0, 0, 6, 3), QRect(4, 2, 2, 1)})
        QVERIFY(!Designer::cropImageElement(original, bounds));
    QVERIFY(!Designer::rotateImageElement(original, 45));
    auto invalid = original;
    invalid.type = ElementType::Box;
    QVERIFY(!Designer::cropImageElement(invalid, invalid.image.rect()));
    QVERIFY(!Designer::rotateImageElement(invalid, 90));
    QVERIFY(!Designer::mirrorImageElement(invalid, Qt::Horizontal));
    invalid = original;
    invalid.image = {};
    QVERIFY(!Designer::rotateImageElement(invalid, 90));
    invalid = original;
    invalid.image = QImage(4097, 1, QImage::Format_Grayscale8);
    QVERIFY(!Designer::mirrorImageElement(invalid, Qt::Vertical));
    invalid = original;
    invalid.width = 4097;
    QVERIFY(!Designer::cropImageElement(invalid, invalid.image.rect()));
}

QTEST_APPLESS_MAIN(ImageOperationsTests)
#include "test_image_operations.moc"
