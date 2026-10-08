#pragma once

#include "design_document.hpp"
#include <QtZpl/qtzpl.hpp>

// Internal coordination between codec, converter and QtZpl adapter; UI code uses
// only design_document.hpp and never depends on the parser's concrete payloads.
namespace Designer::Internal {
inline constexpr int maximumSide = 4096;
inline constexpr qint64 maximumPixels = 16 * 1024 * 1024;
inline constexpr qsizetype maximumSource = 8 * 1024 * 1024;
inline constexpr qsizetype maximumElements = 200;
inline constexpr qsizetype maximumField = 1024;

bool safeSize(int width, int height);
bool safeCoordinate(int value);
QString diagnosticText(const QtZpl::Diagnostic& value);
QString validateElement(const Element& element, bool preservePlaceholders = true);
QString validateParsed(const QtZpl::Document& parsed, qsizetype* errorOffset = nullptr);
std::expected<QtZpl::RenderResult, QString> checkedRender(const QString& source, int dpi);
// Avoid serializing editor snapshots for each canvas/isolated-field render.
QString printerZpl(const DesignDocument& document, bool preservePlaceholders);
} // namespace Designer::Internal
