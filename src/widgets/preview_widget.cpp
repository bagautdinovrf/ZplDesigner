#include "preview_widget.hpp"
#include "designer_theme.hpp"

#include <QGraphicsScene>
#include <QComboBox>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace Designer {
using namespace Qt::StringLiterals;

class PreviewView final : public QGraphicsView {
public:
    explicit PreviewView(QWidget* parent) : QGraphicsView(parent), scene_(this)
    {
        setObjectName(u"previewView"_s);
        setScene(&scene_);
        setFrameShape(QFrame::NoFrame);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setInteractive(false);
        setTransformationAnchor(QGraphicsView::NoAnchor);
        setResizeAnchor(QGraphicsView::AnchorViewCenter);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setRenderHint(QPainter::SmoothPixmapTransform, false);
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
        setAccessibleName(u"Изображение этикетки"_s);
        setToolTip(u"Ctrl + колесо — масштаб; потяните мышью — перемещение вида."_s);
    }

    void setImage(const QImage& image, QString emptyMessage)
    {
        const QPointF center = mapToScene(viewport()->rect().center());
        image_ = image;
        emptyMessage_ = std::move(emptyMessage);
        // Scene units are printer dots, including when a QImage carries a
        // device-pixel ratio originating outside the renderer.
        setSceneRect(image_.isNull() ? QRectF(0, 0, 1, 1) : QRectF(image_.rect()));
        if (fitMode_ && !image_.isNull())
            fitImage();
        else
            centerOn(center);
        viewport()->update();
    }

    [[nodiscard]] bool hasImage() const { return !image_.isNull(); }
    [[nodiscard]] bool isFitMode() const { return fitMode_; }
    [[nodiscard]] qreal zoom() const { return transform().m11(); }

    void fitImage()
    {
        fitMode_ = true;
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        if (hasImage()) {
            const qreal width = std::max(1, viewport()->width() - 70);
            const qreal height = std::max(1, viewport()->height() - 95);
            const qreal scale = std::min(width / image_.width(), height / image_.height());
            setTransform(QTransform::fromScale(scale, scale));
            centerOn(sceneRect().center());
        }
        if (zoomChanged) zoomChanged();
    }

    void setZoom(qreal scale, const QPoint* anchor = nullptr)
    {
        if (!hasImage()) return;
        const QPoint viewportAnchor = anchor ? *anchor : viewport()->rect().center();
        const QPointF sceneAnchor = mapToScene(viewportAnchor);
        fitMode_ = false;
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scale = std::clamp(scale, 0.05, 16.0);
        setTransform(QTransform::fromScale(scale, scale));
        // Keep the part under the pointer (or the view center) in place.
        const QPointF displacement = sceneAnchor - mapToScene(viewportAnchor);
        centerOn(mapToScene(viewport()->rect().center()) + displacement);
        if (zoomChanged) zoomChanged();
    }

    std::function<void()> zoomChanged;

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override
    {
        painter->save();
        painter->fillRect(rect, designerColor(DesignerColor::Canvas));
        if (!hasImage()) {
            painter->restore();
            return;
        }
        painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
        const qreal scale = transform().m11();
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 4));
        for (int spread = 8; spread >= 1; --spread)
            painter->drawRect(sceneRect().adjusted(-spread / scale, -spread / scale,
                                                  spread / scale, spread / scale).translated(0, 3 / scale));
        painter->fillRect(sceneRect(), Qt::white);
        painter->drawImage(sceneRect(), image_, image_.rect());
        painter->restore();
    }

    void drawForeground(QPainter* painter, const QRectF&) override
    {
        if (hasImage()) return;
        painter->save();
        painter->setWorldTransform(viewportTransform().inverted(), true);
        painter->fillRect(viewport()->rect(), palette().brush(QPalette::Base));
        painter->setPen(palette().color(QPalette::Text));
        painter->drawText(viewport()->rect().adjusted(24, 24, -24, -24),
                          Qt::AlignCenter | Qt::TextWordWrap, emptyMessage_);
        painter->restore();
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QGraphicsView::resizeEvent(event);
        if (fitMode_) fitImage();
    }

    void changeEvent(QEvent* event) override
    {
        QGraphicsView::changeEvent(event);
        if (event->type() == QEvent::PaletteChange) viewport()->update();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            const QPoint anchor = event->position().toPoint();
            const qreal steps = event->angleDelta().y() / 120.0;
            if (!qFuzzyIsNull(steps)) setZoom(zoom() * std::pow(1.2, steps), &anchor);
            event->accept();
            return;
        }
        QGraphicsView::wheelEvent(event);
    }

private:
    QGraphicsScene scene_;
    QImage image_;
    QString emptyMessage_ = u"Превью пока нет."_s;
    bool fitMode_ = true;
};

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent)
{
    setObjectName(u"previewWidget"_s);
    setAccessibleName(u"Предпросмотр этикетки"_s);

    auto* layout = new QVBoxLayout(this);
    auto* tools = new QHBoxLayout;
    const auto button = [this, tools](const QString& text, const QString& name) {
        auto* result = new QPushButton(text, this);
        result->setObjectName(name);
        result->setAutoDefault(false);
        tools->addWidget(result);
        return result;
    };
    fit_ = button(u"Вписать"_s, u"previewFit"_s);
    fit_->setCheckable(true);
    actualSize_ = button(u"100%"_s, u"previewActualSize"_s);
    actualSize_->setToolTip(u"Одна точка принтера соответствует одному пикселю вида."_s);
    zoomOut_ = button(u"−"_s, u"previewZoomOut"_s);
    zoomOut_->setAccessibleName(u"Уменьшить масштаб"_s);
    zoomIn_ = button(u"+"_s, u"previewZoomIn"_s);
    zoomIn_->setAccessibleName(u"Увеличить масштаб"_s);
    zoom_ = new QLabel(this);
    zoom_->setObjectName(u"previewZoom"_s);
    zoom_->setMinimumWidth(fontMetrics().horizontalAdvance(u"1600%"_s));
    tools->addWidget(zoom_);
    tools->addStretch();
    export_ = button(u"Сохранить PNG…"_s, u"previewExportPng"_s);
    layout->addLayout(tools);

    pageControls_ = new QWidget(this);
    auto* pageRow = new QHBoxLayout(pageControls_);
    pageRow->setContentsMargins(0, 0, 0, 0);
    auto* pageLabel = new QLabel(u"Этикетка:"_s, pageControls_);
    pages_ = new QComboBox(pageControls_);
    pages_->setObjectName(u"previewPageSelector"_s);
    pages_->setAccessibleName(u"Этикетка в файле ZPL"_s);
    pageLabel->setBuddy(pages_);
    pageRow->addWidget(pageLabel);
    pageRow->addWidget(pages_);
    pageRow->addStretch();
    layout->addWidget(pageControls_);
    connect(pages_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) emit pageRequested(index);
    });
    setPages(1);

    dimensions_ = new QLabel(this);
    dimensions_->setObjectName(u"previewDimensions"_s);
    dimensions_->setTextFormat(Qt::PlainText);
    dimensions_->setWordWrap(true);
    layout->addWidget(dimensions_);
    status_ = new QLabel(this);
    status_->setObjectName(u"previewStatus"_s);
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);

    view_ = new PreviewView(this);
    setFocusProxy(view_);
    layout->addWidget(view_, 1);
    diagnostics_ = new QPlainTextEdit(this);
    diagnostics_->setObjectName(u"previewDiagnostics"_s);
    diagnostics_->setAccessibleName(u"Предупреждения QtZpl"_s);
    diagnostics_->setReadOnly(true);
    diagnostics_->setMaximumHeight(120);
    layout->addWidget(diagnostics_);
    auto* hint = new QLabel(u"Ctrl + колесо — масштаб · Потяните мышью — перемещение вида"_s, this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    view_->zoomChanged = [this] { updateZoomControls(); };
    connect(fit_, &QPushButton::clicked, view_, &PreviewView::fitImage);
    connect(actualSize_, &QPushButton::clicked, this, [this] { view_->setZoom(1.0); });
    connect(zoomIn_, &QPushButton::clicked, this, [this] { view_->setZoom(view_->zoom() * 1.2); });
    connect(zoomOut_, &QPushButton::clicked, this, [this] { view_->setZoom(view_->zoom() / 1.2); });
    connect(export_, &QPushButton::clicked, this, &PreviewWidget::exportRequested);
    setPreview({}, 203);
}

void PreviewWidget::setPreview(const Preview& preview, int dpi, const QString& error)
{
    const bool available = error.isEmpty() && !preview.image.isNull();
    const QString emptyMessage = error.isEmpty()
        ? u"Превью пока нет. Примените ZPL или добавьте элементы на этикетку."_s
        : u"Не удалось построить превью."_s;
    view_->setImage(available ? preview.image : QImage{}, emptyMessage);
    status_->setText(error);
    status_->setVisible(!error.isEmpty());
    export_->setEnabled(available);
    fit_->setEnabled(available);
    actualSize_->setEnabled(available);
    if (available) {
        QString dimensions = u"%1 × %2 dots"_s.arg(preview.image.width()).arg(preview.image.height());
        if (dpi > 0) {
            const QLocale locale;
            dimensions += u"  ·  %1 × %2 мм  ·  %3 dpi"_s
                .arg(locale.toString(preview.image.width() * 25.4 / dpi, 'f', 1),
                     locale.toString(preview.image.height() * 25.4 / dpi, 'f', 1))
                .arg(dpi);
        }
        dimensions_->setText(dimensions);
    } else {
        dimensions_->setText(u"Изображение недоступно"_s);
    }
    const QString diagnostics = preview.diagnostics.join(u'\n');
    if (diagnostics_->toPlainText() != diagnostics) diagnostics_->setPlainText(diagnostics);
    diagnostics_->setVisible(!diagnostics.isEmpty());
    updateZoomControls();
}

void PreviewWidget::updateZoomControls()
{
    const bool available = view_->hasImage();
    fit_->setChecked(view_->isFitMode());
    zoom_->setText(available ? u"%1%"_s.arg(qRound(view_->zoom() * 100.0)) : u"—"_s);
    zoomIn_->setEnabled(available && view_->zoom() < 16.0);
    zoomOut_->setEnabled(available && view_->zoom() > 0.05);
}

void PreviewWidget::setPages(int count, int current)
{
    count = std::clamp(count, 0, 16);
    const QSignalBlocker blocker(pages_);
    if (pages_->count() != count) {
        pages_->clear();
        for (int page = 0; page < count; ++page)
            pages_->addItem(u"%1 из %2"_s.arg(page + 1).arg(count));
    }
    pages_->setCurrentIndex(count > 0 ? std::clamp(current, 0, count - 1) : -1);
    pageControls_->setVisible(count > 1);
}

int PreviewWidget::pageCount() const { return pages_->count(); }
int PreviewWidget::currentPage() const { return pages_->currentIndex(); }

} // namespace Designer
