#include "image_edit_dialog.hpp"
#include "image_operations.hpp"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace Designer {
using namespace Qt::StringLiterals;

class ImageEditPreview final : public QWidget {
public:
    explicit ImageEditPreview(QWidget* parent) : QWidget(parent)
    {
        setObjectName(u"imageEditPreview"_s);
        setAccessibleName(u"Изображение и область обрезки"_s);
        setMinimumSize(320, 240);
        setCursor(Qt::CrossCursor);
    }

    void setImage(const QImage& image, QRect crop)
    {
        image_ = image;
        crop_ = crop;
        update();
    }

    std::function<void(QRect)> cropChanged;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().brush(QPalette::Window));
        if (image_.isNull()) return;
        const QRectF target = imageRect();
        painter.fillRect(target, Qt::white);
        painter.drawImage(target, image_);
        const qreal factor = target.width() / image_.width();
        const QRectF selected(target.topLeft() + QPointF(crop_.x() * factor, crop_.y() * factor),
                              QSizeF(crop_.width() * factor, crop_.height() * factor));
        QPainterPath outside;
        outside.addRect(target);
        outside.addRect(selected);
        painter.fillPath(outside, QColor(0, 0, 0, 120));
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2, Qt::DashLine));
        painter.drawRect(selected);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || image_.isNull() || !imageRect().contains(event->position())) return;
        anchor_ = imagePoint(event->position());
        dragging_ = true;
        selectTo(event->position());
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_) return;
        selectTo(event->position());
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !dragging_) return;
        selectTo(event->position());
        dragging_ = false;
        event->accept();
    }

private:
    [[nodiscard]] QRectF imageRect() const
    {
        const QRectF available = QRectF(rect()).adjusted(12, 12, -12, -12);
        const QSizeF size = QSizeF(image_.size()).scaled(available.size(), Qt::KeepAspectRatio);
        return {available.center() - QPointF(size.width() / 2, size.height() / 2), size};
    }

    [[nodiscard]] QPoint imagePoint(QPointF point) const
    {
        const QRectF target = imageRect();
        const qreal factor = image_.width() / target.width();
        return {std::clamp(int(std::floor((point.x() - target.x()) * factor)), 0, image_.width() - 1),
                std::clamp(int(std::floor((point.y() - target.y()) * factor)), 0, image_.height() - 1)};
    }

    void selectTo(QPointF point)
    {
        const QPoint end = imagePoint(point);
        crop_ = QRect(QPoint(std::min(anchor_.x(), end.x()), std::min(anchor_.y(), end.y())),
                      QPoint(std::max(anchor_.x(), end.x()), std::max(anchor_.y(), end.y())));
        if (cropChanged) cropChanged(crop_);
        update();
    }

    QImage image_;
    QRect crop_;
    QPoint anchor_;
    bool dragging_ = false;
};

ImageEditDialog::ImageEditDialog(const Element& element, QWidget* parent)
    : QDialog(parent), original_(element), working_(element), crop_(element.image.rect())
{
    setObjectName(u"imageEditDialog"_s);
    setWindowTitle(u"Редактирование изображения"_s);
    resize(720, 620);
    auto* layout = new QVBoxLayout(this);
    auto* help = new QLabel(u"Выделите область мышью или задайте обрезку в пикселях. Изменения применятся после подтверждения."_s, this);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto* transforms = new QHBoxLayout;
    const auto button = [this, transforms](const QString& title, const QString& name) {
        auto* control = new QPushButton(title, this);
        control->setObjectName(name);
        control->setAutoDefault(false);
        transforms->addWidget(control);
        return control;
    };
    for (const int angle : {90, 180, 270}) {
        auto* control = button(u"%1°"_s.arg(angle), u"imageRotate%1"_s.arg(angle));
        control->setToolTip(u"Повернуть на %1° по часовой стрелке"_s.arg(angle));
        connect(control, &QPushButton::clicked, this, [this, angle] { rotate(angle); });
    }
    auto* horizontal = button(u"Отразить ↔"_s, u"imageMirrorHorizontal"_s);
    auto* vertical = button(u"Отразить ↕"_s, u"imageMirrorVertical"_s);
    connect(horizontal, &QPushButton::clicked, this, [this] { mirror(Qt::Horizontal); });
    connect(vertical, &QPushButton::clicked, this, [this] { mirror(Qt::Vertical); });
    transforms->addStretch();
    auto* reset = button(u"Сбросить"_s, u"imageEditReset"_s);
    connect(reset, &QPushButton::clicked, this, [this] {
        working_ = original_;
        crop_ = working_.image.rect();
        syncCropControls();
    });
    layout->addLayout(transforms);
    preview_ = new ImageEditPreview(this);
    preview_->cropChanged = [this](QRect crop) { crop_ = crop; syncCropControls(); };
    layout->addWidget(preview_, 1);
    auto* cropControls = new QGridLayout;
    const auto cropSpin = [this, cropControls](const QString& label, const QString& name, int column) {
        auto* control = new QSpinBox(this);
        control->setObjectName(name);
        control->setSuffix(u" px"_s);
        control->setKeyboardTracking(false);
        cropControls->addWidget(new QLabel(label, this), 0, column);
        cropControls->addWidget(control, 1, column);
        connect(control, &QSpinBox::valueChanged, this, [this] { updateCropFromControls(); });
        return control;
    };
    cropX_ = cropSpin(u"Слева"_s, u"imageCropX"_s, 0);
    cropY_ = cropSpin(u"Сверху"_s, u"imageCropY"_s, 1);
    cropWidth_ = cropSpin(u"Ширина"_s, u"imageCropWidth"_s, 2);
    cropHeight_ = cropSpin(u"Высота"_s, u"imageCropHeight"_s, 3);
    layout->addLayout(cropControls);
    dimensions_ = new QLabel(this);
    dimensions_->setObjectName(u"imageEditDimensions"_s);
    dimensions_->setWordWrap(true);
    layout->addWidget(dimensions_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    apply_ = buttons->button(QDialogButtonBox::Ok);
    apply_->setText(u"Применить"_s);
    apply_->setObjectName(u"imageEditApply"_s);
    buttons->button(QDialogButtonBox::Cancel)->setText(u"Отмена"_s);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    syncCropControls();
}

Element ImageEditDialog::editedElement() const
{
    const auto cropped = cropImageElement(working_, crop_);
    return cropped ? *cropped : original_;
}

void ImageEditDialog::rotate(int clockwiseDegrees)
{
    auto result = rotateImageElement(working_, clockwiseDegrees);
    if (!result) { dimensions_->setText(result.error()); return; }
    const int width = working_.image.width();
    const int height = working_.image.height();
    if (clockwiseDegrees == 90)
        crop_ = QRect(height - crop_.y() - crop_.height(), crop_.x(), crop_.height(), crop_.width());
    else if (clockwiseDegrees == 180)
        crop_ = QRect(width - crop_.x() - crop_.width(), height - crop_.y() - crop_.height(), crop_.width(), crop_.height());
    else if (clockwiseDegrees == 270)
        crop_ = QRect(crop_.y(), width - crop_.x() - crop_.width(), crop_.height(), crop_.width());
    working_ = std::move(*result);
    syncCropControls();
}

void ImageEditDialog::mirror(Qt::Orientation axis)
{
    auto result = mirrorImageElement(working_, axis);
    if (!result) { dimensions_->setText(result.error()); return; }
    if (axis == Qt::Horizontal) crop_.moveLeft(working_.image.width() - crop_.x() - crop_.width());
    else crop_.moveTop(working_.image.height() - crop_.y() - crop_.height());
    working_ = std::move(*result);
    syncCropControls();
}

void ImageEditDialog::syncCropControls()
{
    const QSignalBlocker blockX(cropX_), blockY(cropY_), blockWidth(cropWidth_), blockHeight(cropHeight_);
    const int width = std::max(1, working_.image.width());
    const int height = std::max(1, working_.image.height());
    cropX_->setRange(0, width - 1);
    cropY_->setRange(0, height - 1);
    cropX_->setValue(crop_.x());
    cropY_->setValue(crop_.y());
    cropWidth_->setRange(1, width - cropX_->value());
    cropHeight_->setRange(1, height - cropY_->value());
    cropWidth_->setValue(crop_.width());
    cropHeight_->setValue(crop_.height());
    updatePreview();
}

void ImageEditDialog::updateCropFromControls()
{
    crop_ = QRect(cropX_->value(), cropY_->value(),
                  std::min(cropWidth_->value(), working_.image.width() - cropX_->value()),
                  std::min(cropHeight_->value(), working_.image.height() - cropY_->value()));
    syncCropControls();
}

void ImageEditDialog::updatePreview()
{
    preview_->setImage(working_.image, crop_);
    const auto result = cropImageElement(working_, crop_);
    apply_->setEnabled(result.has_value());
    if (!result) { dimensions_->setText(result.error()); return; }
    dimensions_->setText(u"После обрезки: %1 × %2 px · На этикетке: %3 × %4 dots"_s
        .arg(result->image.width()).arg(result->image.height()).arg(result->width).arg(result->height));
}

} // namespace Designer
