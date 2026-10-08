#include "preview_widget.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QtTest>

#include <memory>

using Designer::Preview;
using Designer::PreviewWidget;

class PreviewTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void showsSuppliedRasterWithoutEditorDecorations();
    void manualZoomSurvivesPreviewUpdates();
    void fitFollowsWindowAndImageSize();
    void controlWheelZoomAndMousePanning();
    void errorsClearOldImageAndDisableExport();
    void diagnosticsAndExportRemainAvailable();

private:
    [[nodiscard]] QPushButton* button(const char* name) const;
    [[nodiscard]] static Preview raster(QSize size);

    std::unique_ptr<PreviewWidget> widget_;
    QGraphicsView* view_ = nullptr;
};

Preview PreviewTests::raster(QSize size)
{
    Preview preview;
    preview.image = QImage(size, QImage::Format_RGB32);
    preview.image.fill(Qt::white);
    return preview;
}

QPushButton* PreviewTests::button(const char* name) const
{
    return widget_->findChild<QPushButton*>(QString::fromLatin1(name));
}

void PreviewTests::init()
{
    widget_ = std::make_unique<PreviewWidget>();
    widget_->resize(800, 640);
    widget_->show();
    QApplication::processEvents();
    view_ = widget_->findChild<QGraphicsView*>(QStringLiteral("previewView"));
    QVERIFY(view_);
    QCOMPARE(widget_->objectName(), QStringLiteral("previewWidget"));
    QVERIFY(!widget_->findChild<QDialogButtonBox*>());
    QVERIFY(button("previewFit"));
    QVERIFY(button("previewActualSize"));
    QVERIFY(button("previewZoomIn"));
    QVERIFY(button("previewZoomOut"));
    QVERIFY(button("previewExportPng"));
}

void PreviewTests::cleanup()
{
    widget_.reset();
    view_ = nullptr;
}

void PreviewTests::showsSuppliedRasterWithoutEditorDecorations()
{
    auto preview = raster({203, 406});
    // Bounds are supplied deliberately: they must never become selections or
    // other marks in the clean preview. This is a viewer test, not a renderer.
    preview.bounds.append(QRect(16, 16, 32, 32));
    preview.image.setPixelColor(16, 16, Qt::black);
    preview.image.setPixelColor(17, 16, Qt::red);
    widget_->setPreview(preview, 203);
    QTest::mouseClick(button("previewActualSize"), Qt::LeftButton);
    QApplication::processEvents();
    QCOMPARE(view_->transform().m11(), 1.0);
    QCOMPARE(view_->sceneRect(), QRectF(0, 0, 203, 406));
    QVERIFY(view_->scene()->items().isEmpty());
    const QImage screenshot = view_->viewport()->grab().toImage();
    const qreal dpr = screenshot.devicePixelRatio();
    const auto colorAt = [this, &screenshot, dpr](QPoint point) {
        const QPoint viewPoint = view_->mapFromScene(point);
        return screenshot.pixelColor(qRound(viewPoint.x() * dpr), qRound(viewPoint.y() * dpr));
    };
    QCOMPARE(colorAt({16, 16}), QColor(Qt::black));
    QCOMPARE(colorAt({17, 16}), QColor(Qt::red));
    QCOMPARE(colorAt({32, 16}), QColor(Qt::white));
    QCOMPARE(colorAt({48, 48}), QColor(Qt::white));
    const auto* dimensions = widget_->findChild<QLabel*>(QStringLiteral("previewDimensions"));
    QVERIFY(dimensions);
    QVERIFY(dimensions->text().contains(QStringLiteral("203 × 406 dots")));
    QVERIFY(dimensions->text().contains(QStringLiteral("203 dpi")));
    QVERIFY(dimensions->text().contains(QStringLiteral("мм")));
}

void PreviewTests::manualZoomSurvivesPreviewUpdates()
{
    widget_->setPreview(raster({400, 300}), 203);
    QTest::mouseClick(button("previewActualSize"), Qt::LeftButton);
    QTest::mouseClick(button("previewZoomIn"), Qt::LeftButton);
    const qreal scale = view_->transform().m11();
    QVERIFY(scale > 1.0);
    QVERIFY(!button("previewFit")->isChecked());
    widget_->setPreview(raster({600, 360}), 300);
    widget_->resize(920, 720);
    QApplication::processEvents();
    QCOMPARE(view_->transform().m11(), scale);
    QVERIFY(!button("previewFit")->isChecked());

    // A temporary failure must not discard the user's chosen viewing scale.
    widget_->setPreview({}, 300, QStringLiteral("Ошибка рендера"));
    widget_->setPreview(raster({600, 360}), 300);
    QApplication::processEvents();
    QCOMPARE(view_->transform().m11(), scale);
}

void PreviewTests::fitFollowsWindowAndImageSize()
{
    widget_->setPreview(raster({1200, 900}), 203);
    QApplication::processEvents();
    const qreal before = view_->transform().m11();
    QVERIFY(button("previewFit")->isChecked());
    widget_->resize(1000, 800);
    QApplication::processEvents();
    QVERIFY(view_->transform().m11() > before);
    const qreal resized = view_->transform().m11();
    widget_->setPreview(raster({2400, 1800}), 203);
    QApplication::processEvents();
    QVERIFY(qAbs(view_->transform().m11() - resized / 2.0) < 0.001);
    const QRect visible = view_->mapFromScene(view_->sceneRect()).boundingRect();
    QVERIFY(view_->viewport()->rect().contains(visible));
    QCOMPARE(view_->horizontalScrollBar()->maximum(), 0);
    QCOMPARE(view_->verticalScrollBar()->maximum(), 0);
}

void PreviewTests::controlWheelZoomAndMousePanning()
{
    widget_->setPreview(raster({1600, 1200}), 203);
    QTest::mouseClick(button("previewActualSize"), Qt::LeftButton);
    QApplication::processEvents();
    const QPoint cursor = view_->viewport()->rect().center();
    QWheelEvent wheel(cursor, view_->viewport()->mapToGlobal(cursor), {}, {0, 120},
                      Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view_->viewport(), &wheel);
    QVERIFY(view_->transform().m11() > 1.0);
    QVERIFY(!button("previewFit")->isChecked());
    QVERIFY(view_->horizontalScrollBar()->maximum() > 0);
    QVERIFY(view_->verticalScrollBar()->maximum() > 0);
    const int x = view_->horizontalScrollBar()->value();
    const int y = view_->verticalScrollBar()->value();
    QTest::mousePress(view_->viewport(), Qt::LeftButton, Qt::NoModifier, cursor);
    QTest::mouseMove(view_->viewport(), cursor + QPoint(40, 30));
    QTest::mouseRelease(view_->viewport(), Qt::LeftButton, Qt::NoModifier, cursor + QPoint(40, 30));
    QVERIFY(view_->horizontalScrollBar()->value() < x);
    QVERIFY(view_->verticalScrollBar()->value() < y);
}

void PreviewTests::errorsClearOldImageAndDisableExport()
{
    QSignalSpy exported(widget_.get(), &PreviewWidget::exportRequested);
    QVERIFY(!button("previewExportPng")->isEnabled());
    widget_->setPreview(raster({400, 300}), 203);
    QVERIFY(button("previewExportPng")->isEnabled());
    // Even if a caller still holds an old successful raster, an error must
    // suppress it, rather than present stale pixels as the current result.
    widget_->setPreview(raster({400, 300}), 203, QStringLiteral("Неверный ZPL <test>"));
    QVERIFY(!button("previewExportPng")->isEnabled());
    QVERIFY(!button("previewActualSize")->isEnabled());
    QVERIFY(!button("previewZoomIn")->isEnabled());
    QCOMPARE(view_->sceneRect(), QRectF(0, 0, 1, 1));
    const auto* status = widget_->findChild<QLabel*>(QStringLiteral("previewStatus"));
    QVERIFY(status);
    QVERIFY(status->isVisible());
    QCOMPARE(status->text(), QStringLiteral("Неверный ZPL <test>"));
    QCOMPARE(status->textFormat(), Qt::PlainText);
    QTest::mouseClick(button("previewExportPng"), Qt::LeftButton);
    QVERIFY(exported.isEmpty());
    widget_->setPreview({}, 0);
    QVERIFY(!button("previewExportPng")->isEnabled());
    QVERIFY(!status->isVisible());
}

void PreviewTests::diagnosticsAndExportRemainAvailable()
{
    auto preview = raster({400, 300});
    preview.diagnostics = {QStringLiteral("Показана первая этикетка."),
                           QStringLiteral("Шрифт заменён в превью.")};
    widget_->setPreview(preview, 203);
    const auto* diagnostics = widget_->findChild<QPlainTextEdit*>(QStringLiteral("previewDiagnostics"));
    QVERIFY(diagnostics);
    QVERIFY(diagnostics->isVisible());
    QVERIFY(diagnostics->isReadOnly());
    QCOMPARE(diagnostics->toPlainText(), preview.diagnostics.join(u'\n'));
    QSignalSpy exported(widget_.get(), &PreviewWidget::exportRequested);
    QTest::mouseClick(button("previewExportPng"), Qt::LeftButton);
    QCOMPARE(exported.size(), 1);
    widget_->setPreview(raster({400, 300}), 203);
    QVERIFY(!diagnostics->isVisible());
    QVERIFY(diagnostics->toPlainText().isEmpty());
}

QTEST_MAIN(PreviewTests)
#include "test_preview.moc"
