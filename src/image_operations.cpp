#include "image_operations.hpp"

#include <QTransform>

#include <algorithm>
#include <utility>

namespace Designer {
namespace {
using namespace Qt::StringLiterals;

std::expected<void, QString> validateImage(const Element& element)
{
    if (element.type != ElementType::Image || element.image.isNull())
        return std::unexpected(u"Выберите элемент с изображением."_s);
    const auto validSize = [](int width, int height) {
        return width > 0 && height > 0 && width <= 4096 && height <= 4096;
    };
    if (!validSize(element.image.width(), element.image.height()) || !validSize(element.width, element.height))
        return std::unexpected(u"Размер изображения и печатного элемента не должен превышать 4096 × 4096."_s);
    return {};
}
} // namespace

std::expected<Element, QString> cropImageElement(const Element& element, QRect bounds)
{
    using namespace Qt::StringLiterals;
    if (auto valid = validateImage(element); !valid) return std::unexpected(valid.error());
    if (bounds.isEmpty() || !element.image.rect().contains(bounds))
        return std::unexpected(u"Область обрезки должна находиться внутри изображения."_s);
    if (bounds == element.image.rect()) return element;
    auto result = element;
    result.image = element.image.copy(bounds);
    if (result.image.isNull()) return std::unexpected(u"Не удалось обрезать изображение."_s);
    result.width = std::max(1, qRound(double(element.width) * bounds.width() / element.image.width()));
    result.height = std::max(1, qRound(double(element.height) * bounds.height() / element.image.height()));
    return result;
}

std::expected<Element, QString> rotateImageElement(const Element& element, int clockwiseDegrees)
{
    using namespace Qt::StringLiterals;
    if (auto valid = validateImage(element); !valid) return std::unexpected(valid.error());
    if (clockwiseDegrees != 0 && clockwiseDegrees != 90 && clockwiseDegrees != 180 && clockwiseDegrees != 270)
        return std::unexpected(u"Выберите поворот на 90°, 180° или 270°."_s);
    if (clockwiseDegrees == 0) return element;
    auto result = element;
    result.image = element.image.transformed(QTransform{}.rotate(clockwiseDegrees), Qt::FastTransformation);
    if (result.image.isNull()) return std::unexpected(u"Не удалось повернуть изображение."_s);
    if (clockwiseDegrees == 90 || clockwiseDegrees == 270) std::swap(result.width, result.height);
    return result;
}

std::expected<Element, QString> mirrorImageElement(const Element& element, Qt::Orientation axis)
{
    using namespace Qt::StringLiterals;
    if (auto valid = validateImage(element); !valid) return std::unexpected(valid.error());
    if (axis != Qt::Horizontal && axis != Qt::Vertical)
        return std::unexpected(u"Выберите горизонтальное или вертикальное отражение."_s);
    auto result = element;
    result.image = element.image.transformed(
        QTransform::fromScale(axis == Qt::Horizontal ? -1 : 1, axis == Qt::Vertical ? -1 : 1),
        Qt::FastTransformation);
    if (result.image.isNull()) return std::unexpected(u"Не удалось отразить изображение."_s);
    return result;
}

} // namespace Designer
