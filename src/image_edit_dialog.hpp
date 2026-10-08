#pragma once

#include "design_document.hpp"

#include <QDialog>

class QLabel;
class QPushButton;
class QSpinBox;

namespace Designer {

class ImageEditPreview;

class ImageEditDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ImageEditDialog(const Element& element, QWidget* parent = nullptr);
    [[nodiscard]] Element editedElement() const;

private:
    void rotate(int clockwiseDegrees);
    void mirror(Qt::Orientation axis);
    void syncCropControls();
    void updateCropFromControls();
    void updatePreview();

    Element original_;
    Element working_;
    QRect crop_;
    ImageEditPreview* preview_ = nullptr;
    QSpinBox* cropX_ = nullptr;
    QSpinBox* cropY_ = nullptr;
    QSpinBox* cropWidth_ = nullptr;
    QSpinBox* cropHeight_ = nullptr;
    QLabel* dimensions_ = nullptr;
    QPushButton* apply_ = nullptr;
};

} // namespace Designer
