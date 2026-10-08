#pragma once

#include "design_document.hpp"

namespace Designer {

// Crop bounds refer to source pixels. Print geometry stays in printer dots.
[[nodiscard]] std::expected<Element, QString> cropImageElement(const Element& element, QRect bounds);
[[nodiscard]] std::expected<Element, QString> rotateImageElement(const Element& element, int clockwiseDegrees);
[[nodiscard]] std::expected<Element, QString> mirrorImageElement(const Element& element, Qt::Orientation axis);

} // namespace Designer
