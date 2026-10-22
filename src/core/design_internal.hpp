#pragma once

#include "design_document.hpp"
#include "zpl_source_map.hpp"
#include <QtZpl/qtzpl.hpp>

#include <stop_token>

// Internal coordination between codec, converter and QtZpl adapter; UI code uses
// only design_document.hpp and never depends on the parser's concrete payloads.
namespace Designer::Internal {
inline constexpr int maximumSide = 4096;
inline constexpr qint64 maximumPixels = 16 * 1024 * 1024;
inline constexpr qsizetype maximumSource = 8 * 1024 * 1024;
inline constexpr qsizetype maximumElements = 200;
inline constexpr qsizetype maximumField = 1024;
// Decoded ^GF bytes in one render: four graphics covering a whole maximum label.
inline constexpr qsizetype maximumGraphicBytes = 4 * (maximumPixels / 8);

// Every QtZpl parse, render and analysis call uses these Designer budgets.
QtZpl::ParseOptions parseOptions(std::stop_token stop = {});
QtZpl::RenderOptions renderOptions(int dpi, bool fieldGeometry = false, std::stop_token stop = {});
// Parses the exact text once: the generated ZPL is read by previews, source
// navigation, diagnostics and export checks. QtZpl documents are immutable and
// safe to share across threads; failures, including cancellation, are not kept.
std::expected<QtZpl::Document, QtZpl::ParseError> parseZpl(const QString& source, std::stop_token stop = {});

// A document's generated ZPL, serialized on first use and then shared by the
// export checks, previews and diagnostics of one request.
class TemplateSource final {
public:
    explicit TemplateSource(const DesignDocument& document, bool preservePlaceholders = true)
        : document_(document), preservePlaceholders_(preservePlaceholders) {}
    [[nodiscard]] const DesignDocument& document() const { return document_; }
    [[nodiscard]] bool preservePlaceholders() const { return preservePlaceholders_; }
    [[nodiscard]] const QString& text() const;

private:
    const DesignDocument& document_;
    bool preservePlaceholders_;
    mutable std::optional<QString> text_;
};
// validateForExport() for the document of an already shared template source.
std::expected<void, QString> validateTemplate(const TemplateSource& source);

bool safeSize(int width, int height);
bool safeCoordinate(int value);
QString diagnosticText(const QtZpl::Diagnostic& value);
QString validateElement(const Element& element, bool preservePlaceholders = true, int dpi = 0);
QString validateParsed(const QtZpl::Document& parsed, qsizetype* errorOffset = nullptr);
std::expected<QtZpl::RenderResult, QString> checkedRender(const QString& source, int dpi);
// QtZpl command spans reach the next command prefix, separating whitespace
// included. This is the end of the command's own text in document.source(),
// so a removed, replaced or followed command keeps the surrounding line breaks.
qsizetype commandEnd(const QtZpl::Document& document, const QtZpl::Command& command);
// sourceElementRanges() for a document that is already parsed.
QList<SourceElementRange> elementFieldRanges(const DesignDocument& document, const QtZpl::Document& parsed);
// The element whose ^FO...^FS range contains a source offset, or -1.
int rangeOwner(const QList<SourceElementRange>& ranges, qsizetype offset);
// Avoid serializing editor snapshots for each canvas/isolated-field render.
QString printerZpl(const DesignDocument& document, bool preservePlaceholders);
} // namespace Designer::Internal
