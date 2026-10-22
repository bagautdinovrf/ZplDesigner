#include "design_internal.hpp"

#include <QMutex>

namespace Designer::Internal {
namespace {
// Most recent first. A preview refresh reads two or three distinct sources.
constexpr qsizetype parsedCacheSize = 4;
QMutex parsedMutex;
QList<QtZpl::Document> parsedCache;
} // namespace

QtZpl::ParseOptions parseOptions(std::stop_token stop)
{
    QtZpl::ParseOptions options;
    options.maxSourceLength = maximumSource;
    // Bounds memory before validateParsed reports the 16-label product limit.
    options.maxLabels = 1024;
    options.stopToken = std::move(stop);
    return options;
}

QtZpl::RenderOptions renderOptions(int dpi, bool fieldGeometry, std::stop_token stop)
{
    QtZpl::RenderOptions options;
    options.dpi = dpi;
    options.collectFieldGeometry = fieldGeometry;
    options.maxTotalPixels = maximumPixels;
    options.maxGraphicBytes = maximumGraphicBytes;
    options.stopToken = std::move(stop);
    return options;
}

std::expected<QtZpl::Document, QtZpl::ParseError> parseZpl(const QString& source, std::stop_token stop)
{
    {
        const QMutexLocker lock(&parsedMutex);
        for (qsizetype index = 0; index < parsedCache.size(); ++index) {
            if (parsedCache[index].source() != source) continue;
            if (index > 0) parsedCache.move(index, 0);
            return parsedCache.first();
        }
    }
    auto parsed = QtZpl::parse(source, parseOptions(std::move(stop)));
    if (!parsed) return parsed;
    const QMutexLocker lock(&parsedMutex);
    parsedCache.prepend(*parsed);
    if (parsedCache.size() > parsedCacheSize) parsedCache.removeLast();
    return parsed;
}

} // namespace Designer::Internal
