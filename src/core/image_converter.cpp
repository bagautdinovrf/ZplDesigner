#include "design_document.hpp"
#include "design_internal.hpp"

#include <algorithm>
#include <type_traits>
#include <QFileInfo>
#include <QImageReader>

namespace Designer {
using namespace Internal;
QImage monochromeImage(const Element& element)
{
    if (element.image.isNull() || !safeSize(element.width, element.height)
        || !safeSize(element.image.width(), element.image.height())) return {};
    const QImage source = element.image.scaled(element.width, element.height, Qt::IgnoreAspectRatio,
                                               Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
    if (source.isNull()) return {};
    QImage result(source.size(), QImage::Format_Grayscale8);
    if (result.isNull()) return {};
    QList<double> current(source.width() + 2, 0.0), next(source.width() + 2, 0.0);
    const int threshold = std::clamp(element.threshold, 0, 255);
    for (int y = 0; y < source.height(); ++y) {
        const auto* input = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        auto* output = result.scanLine(y);
        for (int x = 0; x < source.width(); ++x) {
            const int alpha = qAlpha(input[x]);
            const double gray = (qGray(input[x]) * alpha + 255.0 * (255 - alpha)) / 255.0;
            const double adjusted = gray + (element.dither ? current[x + 1] : 0.0);
            const int bit = adjusted < threshold ? 0 : 255;
            output[x] = static_cast<uchar>(element.reverse ? 255 - bit : bit);
            if (element.dither) {
                const double error = std::clamp(adjusted, 0.0, 255.0) - bit;
                current[x + 2] += error * 7.0 / 16.0;
                next[x] += error * 3.0 / 16.0;
                next[x + 1] += error * 5.0 / 16.0;
                next[x + 2] += error / 16.0;
            }
        }
        current.swap(next);
        std::fill(next.begin(), next.end(), 0.0);
    }
    return result;
}

std::expected<QImage, QString> loadImage(const QString& path)
{
    using namespace Qt::StringLiterals;
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0 || info.size() > 32 * 1024 * 1024)
        return std::unexpected(u"Изображение недоступно или превышает 32 МиБ."_s);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!size.isValid() || !safeSize(size.width(), size.height()))
        return std::unexpected(u"Изображение должно иметь размер не более 4096 × 4096 пикселей."_s);
    QImage image = reader.read();
    if (image.isNull()) return std::unexpected(reader.errorString());
    if (!safeSize(image.width(), image.height())) return std::unexpected(u"Изображение слишком велико."_s);
    return image;
}
} // namespace Designer
