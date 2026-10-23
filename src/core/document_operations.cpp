#include "document_operations.hpp"
#include "eac_mark.hpp"

#include <QSet>

#include <algorithm>
#include <array>
#include <numeric>
#include <utility>

namespace Designer {
namespace {
constexpr std::array linearBarcodeTypes{
    ElementType::Code128, ElementType::Code39, ElementType::Code93,
    ElementType::Ean13, ElementType::Ean8, ElementType::UpcA, ElementType::UpcE,
    ElementType::Codabar, ElementType::Interleaved2of5, ElementType::Industrial2of5,
    ElementType::Gs1DataBar};
constexpr std::array matrixBarcodeTypes{
    ElementType::DataMatrix, ElementType::QrCode, ElementType::Pdf417,
    ElementType::MicroPdf417, ElementType::Aztec, ElementType::MaxiCode};

std::expected<qsizetype, QString> editableIndex(const DesignDocument& document, const QUuid& id,
                                             bool allowLocked = false)
{
    if (!document.visualEditable)
        return std::unexpected(QStringLiteral("Этот ZPL доступен только в режиме исходного кода."));
    const auto found = std::ranges::find(document.elements, id, &Element::id);
    if (id.isNull() || found == document.elements.cend())
        return std::unexpected(QStringLiteral("Элемент больше не существует в документе."));
    if (found->locked && !allowLocked)
        return std::unexpected(QStringLiteral("Сначала разблокируйте выбранные слои."));
    return std::distance(document.elements.cbegin(), found);
}

template<typename T>
bool assign(T& target, const T& value)
{
    if (target == value) return false;
    target = value;
    return true;
}

template<typename T>
bool assign(T& target, const std::optional<T>& value)
{
    return value && assign(target, *value);
}

template<typename... Members>
bool copyMembers(Element& target, const Element& source, Members... members)
{
    // Bitwise OR intentionally visits all fields, including after one changes.
    return (assign(target.*members, source.*members) | ...);
}

bool within(const std::optional<int>& value, int low, int high)
{
    return !value || (*value >= low && *value <= high);
}

bool oneOf(const std::optional<QChar>& value, QStringView allowed)
{
    return !value || allowed.contains(*value);
}

bool validProperties(const Element& before, const ElementProperties& values)
{
    // Imported inactive values can outlive their original barcode type. A
    // harmless edit must not revalidate or normalize unchanged stored fields.
    const auto number = [](const std::optional<int>& value, int current, int low, int high) {
        return !value || *value == current || within(value, low, high);
    };
    const auto character = [](const std::optional<QChar>& value, QChar current, QStringView allowed) {
        return !value || *value == current || oneOf(value, allowed);
    };
    return number(values.x, before.x, -4096, 4096) && number(values.y, before.y, -4096, 4096)
        && (!values.eacStyle || (before.type == ElementType::Eac
            && static_cast<unsigned>(*values.eacStyle) <= static_cast<unsigned>(EacStyle::WhiteTransparent)))
        && (before.type != ElementType::Eac
            || ((!values.orientation || *values.orientation == u'N') && (!values.reverse || !*values.reverse)))
        && number(values.fontHeight, before.fontHeight, 1, 512) && number(values.fontWidth, before.fontWidth, 0, 512)
        && number(values.thickness, before.thickness, 1, 4096) && number(values.moduleWidth, before.moduleWidth, 1, 10)
        && number(values.threshold, before.threshold, 0, 255) && character(values.orientation, before.orientation, u"NRIB")
        && character(values.diagonalDirection, before.diagonalDirection, u"LR") && number(values.blockWidth, before.blockWidth, 0, 4096)
        && number(values.blockMaxLines, before.blockMaxLines, 1, 128) && number(values.blockLineSpacing, before.blockLineSpacing, -512, 512)
        && number(values.blockHangingIndent, before.blockHangingIndent, -4096, 4096)
        && character(values.blockJustification, before.blockJustification, u"LCRJ") && character(values.code128Mode, before.code128Mode, u"NABCDU")
        && character(values.codabarStart, before.codabarStart, u"ABCD") && character(values.codabarStop, before.codabarStop, u"ABCD")
        && character(values.qrErrorCorrection, before.qrErrorCorrection, u"LMQH") && number(values.pdf417RowHeight, before.pdf417RowHeight, 1, 64)
        && number(values.pdf417Columns, before.pdf417Columns, 0, 30) && number(values.pdf417Security, before.pdf417Security, 0, 8);
}

bool updateProperties(Element& element, const ElementProperties& values)
{
    bool changed = false;
    changed |= assign(element.x, values.x);
    changed |= assign(element.y, values.y);
    if (values.text && element.text != *values.text) {
        element.text = *values.text;
        element.literalText = false;
        changed = true;
    }
    changed |= assign(element.fontHeight, values.fontHeight);
    changed |= assign(element.fontWidth, values.fontWidth);
    changed |= assign(element.thickness, values.thickness);
    changed |= assign(element.moduleWidth, values.moduleWidth);
    changed |= assign(element.threshold, values.threshold);
    changed |= assign(element.orientation, values.orientation);
    changed |= assign(element.humanReadable, values.humanReadable);
    changed |= assign(element.reverse, values.reverse);
    changed |= assign(element.dither, values.dither);
    changed |= assign(element.graphicWhite, values.graphicWhite);
    changed |= assign(element.eacStyle, values.eacStyle);
    changed |= assign(element.diagonalDirection, values.diagonalDirection);
    changed |= assign(element.imageKeepAspectRatio, values.imageKeepAspectRatio);
    if (values.gs1) changed |= assign(element.gs1, element.type == ElementType::DataMatrix && *values.gs1);
    changed |= assign(element.blockWidth, values.blockWidth);
    changed |= assign(element.blockMaxLines, values.blockMaxLines);
    changed |= assign(element.blockLineSpacing, values.blockLineSpacing);
    changed |= assign(element.blockHangingIndent, values.blockHangingIndent);
    changed |= assign(element.blockJustification, values.blockJustification);
    changed |= assign(element.code128Mode, values.code128Mode);
    changed |= assign(element.codabarStart, values.codabarStart);
    changed |= assign(element.codabarStop, values.codabarStop);
    changed |= assign(element.interpretationAbove, values.interpretationAbove);
    changed |= assign(element.qrErrorCorrection, values.qrErrorCorrection);
    changed |= assign(element.pdf417RowHeight, values.pdf417RowHeight);
    changed |= assign(element.pdf417Columns, values.pdf417Columns);
    changed |= assign(element.pdf417Security, values.pdf417Security);
    return changed;
}

OperationResult replaceChanged(DesignDocument& document, qsizetype index, Element element, bool changed)
{
    if (!changed) return false;
    document.elements[index] = std::move(element);
    document.originalZpl.clear();
    return true;
}

bool isShape(ElementType type)
{
    return type == ElementType::Box || type == ElementType::Line || type == ElementType::Circle
        || type == ElementType::Ellipse || type == ElementType::Diagonal;
}

bool updateBarcodeOptions(Element& target, const Element& source)
{
    bool changed = false;
    if (target.type != ElementType::MaxiCode)
        changed |= assign(target.useDefaultOrientation, source.useDefaultOrientation);
    if (isLinearBarcodeType(target.type)) {
        if (target.type != ElementType::Code39 && target.type != ElementType::Codabar)
            changed |= copyMembers(target, source, &Element::humanReadable, &Element::interpretationAbove);
        if (target.type == ElementType::Code39 || target.type == ElementType::Codabar
            || target.type == ElementType::Interleaved2of5 || target.type == ElementType::Industrial2of5)
            changed |= assign(target.barcodeRatio, source.barcodeRatio);
        if (target.type == ElementType::Code93 || target.type == ElementType::Interleaved2of5)
            changed |= assign(target.barcodeCheckDigit, source.barcodeCheckDigit);
        if (target.type == ElementType::Code128)
            changed |= copyMembers(target, source, &Element::code128Mode, &Element::code128CheckDigit);
        if (target.type == ElementType::Codabar)
            changed |= copyMembers(target, source, &Element::codabarStart, &Element::codabarStop);
    } else if (target.type == ElementType::DataMatrix) {
        changed |= copyMembers(target, source, &Element::dataMatrixRectangular,
                               &Element::dataMatrixColumns, &Element::dataMatrixRows);
    } else if (target.type == ElementType::Pdf417 || target.type == ElementType::MicroPdf417) {
        changed |= assign(target.pdf417RowHeight, source.pdf417RowHeight);
        if (target.type == ElementType::Pdf417)
            changed |= copyMembers(target, source, &Element::pdf417Security, &Element::pdf417Columns,
                                   &Element::pdf417Rows, &Element::pdf417Truncated);
        else changed |= assign(target.microPdf417Mode, source.microPdf417Mode);
    } else if (target.type == ElementType::Gs1DataBar) {
        changed |= copyMembers(target, source, &Element::dataBarType, &Element::dataBarSegments,
                               &Element::dataBarSeparator, &Element::height);
    } else if (target.type == ElementType::Aztec) {
        changed |= copyMembers(target, source, &Element::aztecSize, &Element::aztecReaderInit);
    } else if (target.type == ElementType::MaxiCode) {
        changed |= copyMembers(target, source, &Element::maxiCodeMode,
                               &Element::maxiCodePosition, &Element::maxiCodeTotal);
    } else if (target.type == ElementType::QrCode) {
        changed |= assign(target.qrErrorCorrection, source.qrErrorCorrection);
    }
    return changed;
}

bool validTableContent(const Element& element)
{
    if (element.tableColumnWidths.isEmpty() || element.tableColumnWidths.size() > 12
        || element.tableRowHeights.isEmpty() || element.tableRowHeights.size() > 12
        || element.tableCells.size() != element.tableColumnWidths.size() * element.tableRowHeights.size()
        || element.tablePadding < 0 || element.tablePadding > 128
        || element.fontHeight < 1 || element.fontHeight > 512
        || element.thickness < 1 || element.thickness > 4096
        || !QStringView{u"LCRJ"}.contains(element.blockJustification)) return false;
    const auto width = std::accumulate(element.tableColumnWidths.cbegin(), element.tableColumnWidths.cend(), qint64{0});
    const auto height = std::accumulate(element.tableRowHeights.cbegin(), element.tableRowHeights.cend(), qint64{0});
    if (width <= 0 || width > 4096 || height <= 0 || height > 4096) return false;
    return std::ranges::all_of(element.tableColumnWidths, [&](int value) {
        return value >= 8 && value > 2 * element.tablePadding
            && (!element.tableBorders || value >= element.thickness);
    }) && std::ranges::all_of(element.tableRowHeights, [&](int value) {
        return value >= 8 && value >= element.fontHeight + 2 * element.tablePadding
            && (!element.tableBorders || value >= element.thickness);
    }) && std::ranges::all_of(element.tableCells, [](const QString& value) { return value.size() <= 1024; });
}
} // namespace

std::span<const ElementType> availableBarcodeTypes(ElementType current)
{
    if (isLinearBarcodeType(current) || current == ElementType::Gs1DataBar) return linearBarcodeTypes;
    if (isBarcodeType(current)) return matrixBarcodeTypes;
    return {};
}

OperationResult applyOperation(DesignDocument& document, const ChangeBarcodeType& request)
{
    const auto index = editableIndex(document, request.id);
    if (!index) return std::unexpected(index.error());
    auto element = document.elements[*index];
    if (!std::ranges::contains(availableBarcodeTypes(element.type), request.type))
        return std::unexpected(QStringLiteral("Этот тип штрихкода недоступен для выбранного элемента."));
    if (element.type == request.type) return false;
    if (element.name == elementTypeName(element.type)) element.name = elementTypeName(request.type);
    element.type = request.type;
    if (request.type != ElementType::DataMatrix) element.gs1 = false;
    if (request.type == ElementType::Code39 || request.type == ElementType::Codabar) {
        element.humanReadable = false;
        element.interpretationAbove = false;
    }
    if (request.type == ElementType::MaxiCode) {
        element.orientation = u'N';
        element.useDefaultOrientation = false;
        element.width = 201;
        element.height = 195;
    }
    return replaceChanged(document, *index, std::move(element), true);
}

OperationResult applyOperation(DesignDocument& document, const UpdateText& request)
{
    const auto index = editableIndex(document, request.id);
    if (!index) return std::unexpected(index.error());
    auto element = document.elements[*index];
    if (element.type != ElementType::Text && !isBarcodeType(element.type))
        return std::unexpected(QStringLiteral("У выбранного элемента нет редактируемого текста."));
    if (request.gs1 && element.type != ElementType::DataMatrix)
        return std::unexpected(QStringLiteral("Режим GS1 применяется к DataMatrix."));
    const bool textChanged = assign(element.text, request.text);
    if (textChanged && !request.preserveLiteralText) element.literalText = false;
    const bool modeChanged = assign(element.gs1, request.gs1);
    return replaceChanged(document, *index, std::move(element), textChanged || modeChanged);
}

OperationResult applyOperation(DesignDocument& document, const UpdateElementProperties& request)
{
    const auto index = editableIndex(document, request.id);
    if (!index) return std::unexpected(index.error());
    if (!validProperties(document.elements[*index], request.properties))
        return std::unexpected(QStringLiteral("Параметры элемента превышают допустимые пределы."));
    auto element = document.elements[*index];
    if (element.type == ElementType::MaxiCode && request.properties.orientation
        && *request.properties.orientation != element.orientation && *request.properties.orientation != u'N')
        return std::unexpected(QStringLiteral("MaxiCode имеет фиксированную ориентацию."));
    bool changed = updateProperties(element, request.properties);
    if (element.type == ElementType::Eac) {
        const auto& before = document.elements[*index];
        const int requested = before.eacStyle == element.eacStyle ? element.width
            : element.width / eacGridUnits(before.eacStyle) * eacGridUnits(element.eacStyle);
        const int size = eacSizeForRequest(requested, document.dpi, element.eacStyle);
        changed |= assign(element.width, size);
        changed |= assign(element.height, size);
    }
    return replaceChanged(document, *index, std::move(element), changed);
}

OperationResult applyOperation(DesignDocument& document, const UpdateSelectionProperties& request)
{
    const auto& values = request.values;
    if (!within(values.moduleWidth, 1, 10) || !within(values.thickness, 1, 4096)
        || !within(values.fontHeight, 1, 512) || !within(values.fontWidth, 0, 512)
        || !within(values.blockWidth, 0, 4096) || !within(values.blockMaxLines, 1, 128)
        || !within(values.blockLineSpacing, -512, 512) || !within(values.blockHangingIndent, -4096, 4096))
        return std::unexpected(QStringLiteral("Общие параметры выделения превышают допустимые пределы."));
    QList<qsizetype> indices;
    QSet<QUuid> seen;
    for (const auto& id : request.ids) {
        if (seen.contains(id)) continue;
        const auto index = editableIndex(document, id);
        if (!index) return std::unexpected(index.error());
        seen.insert(id);
        indices.append(*index);
    }
    auto next = document;
    bool changed = false;
    for (const auto index : indices) {
        auto& element = next.elements[index];
        if (isBarcodeType(element.type) && element.type != ElementType::MaxiCode)
            changed |= assign(element.moduleWidth, values.moduleWidth);
        if (isShape(element.type)) changed |= assign(element.thickness, values.thickness);
        if (element.type != ElementType::Text) continue;
        changed |= assign(element.fontHeight, values.fontHeight);
        changed |= assign(element.fontWidth, values.fontWidth);
        changed |= assign(element.blockWidth, values.blockWidth);
        changed |= assign(element.blockMaxLines, values.blockMaxLines);
        changed |= assign(element.blockLineSpacing, values.blockLineSpacing);
        changed |= assign(element.blockHangingIndent, values.blockHangingIndent);
    }
    if (changed) {
        next.originalZpl.clear();
        document = std::move(next);
    }
    return changed;
}

OperationResult applyOperation(DesignDocument& document, const ResizeElement& request)
{
    const auto index = editableIndex(document, request.id);
    if (!index) return std::unexpected(index.error());
    if (!request.geometryError.isEmpty())
        return std::unexpected(QStringLiteral("Для изменения размера штрихкода сначала заполните его данные через «Значения плейсхолдеров…»."));
    const auto& before = document.elements[*index];
    if (before.type == ElementType::MaxiCode) return false;
    if ((before.type == ElementType::Text || isBarcodeType(before.type)) && request.currentSize.isEmpty())
        return std::unexpected(QStringLiteral("Сначала обновите геометрию элемента."));
    auto effective = before;
    if (effective.useDefaultOrientation) effective.orientation = document.defaultOrientation;
    auto resized = resizedElement(effective, request.requestedSize, request.currentSize);
    if (resized.type == ElementType::Eac)
        resized.width = resized.height = eacSizeForRequest(resized.width, document.dpi, resized.eacStyle);
    resized.orientation = before.orientation;
    const bool changed = resized.width != before.width || resized.height != before.height
        || resized.fontWidth != before.fontWidth || resized.fontHeight != before.fontHeight
        || resized.blockWidth != before.blockWidth || resized.blockLineSpacing != before.blockLineSpacing
        || resized.blockHangingIndent != before.blockHangingIndent
        || resized.moduleWidth != before.moduleWidth || resized.pdf417RowHeight != before.pdf417RowHeight
        || resized.tableColumnWidths != before.tableColumnWidths || resized.tableRowHeights != before.tableRowHeights;
    if (!changed) return false;
    auto next = document;
    next.elements[*index] = std::move(resized);
    next.originalZpl.clear();
    if (const auto valid = validateForExport(next); !valid && !renderEditorPreview(next))
        return std::unexpected(QStringLiteral("Размер не изменён: %1").arg(valid.error()));
    document = std::move(next);
    return true;
}

OperationResult applyOperation(DesignDocument& document, const UpdateElementContent& request)
{
    const auto index = editableIndex(document, request.id);
    if (!index) return std::unexpected(index.error());
    auto element = document.elements[*index];
    const auto& values = request.values;
    if (values.type != element.type)
        return std::unexpected(QStringLiteral("Тип элемента изменился после открытия диалога."));
    bool changed = false;
    if (element.type == ElementType::Image) {
        if (values.image.isNull() || values.image.width() > 4096 || values.image.height() > 4096
            || values.width < 1 || values.width > 4096 || values.height < 1 || values.height > 4096)
            return std::unexpected(QStringLiteral("Изображение или его размеры превышают допустимые пределы."));
        changed = copyMembers(element, values, &Element::image, &Element::width, &Element::height);
    } else if (element.type == ElementType::Table) {
        if (!validTableContent(values))
            return std::unexpected(QStringLiteral("Проверьте размеры и содержимое таблицы."));
        if (element.tableCells != values.tableCells) element.literalText = false;
        changed = copyMembers(element, values, &Element::tableColumnWidths, &Element::tableRowHeights,
            &Element::tableCells, &Element::tablePadding, &Element::tableBorders,
            &Element::fontHeight, &Element::thickness, &Element::blockJustification);
        element.width = std::accumulate(element.tableColumnWidths.cbegin(), element.tableColumnWidths.cend(), 0);
        element.height = std::accumulate(element.tableRowHeights.cbegin(), element.tableRowHeights.cend(), 0);
        changed |= element.width != document.elements[*index].width || element.height != document.elements[*index].height;
    } else if (element.type == ElementType::Text) {
        changed = copyMembers(element, values, &Element::baselineOrigin, &Element::originJustification,
            &Element::useDefaultOrientation, &Element::orientation, &Element::textDirection, &Element::characterSpacing);
    } else if (isBarcodeType(element.type)) {
        changed = updateBarcodeOptions(element, values);
    } else {
        return std::unexpected(QStringLiteral("Для этого элемента нет параметров содержимого."));
    }
    if (changed && (element.type == ElementType::Text || isBarcodeType(element.type))) {
        DesignDocument check;
        check.elements = {element};
        check.elements.front().printable = true;
        if (const auto valid = validateForExport(check); !valid) return std::unexpected(valid.error());
    }
    return replaceChanged(document, *index, std::move(element), changed);
}

OperationResult applyOperation(DesignDocument& document, const UpdateLabelSettings& request)
{
    if (!document.visualEditable)
        return std::unexpected(QStringLiteral("Этот ZPL доступен только в режиме исходного кода."));
    if (request.width < 1 || request.width > 4096 || request.height < 1 || request.height > 4096
        || (request.dpi != 203 && request.dpi != 300 && request.dpi != 600)
        || !QStringView{u"NRIB"}.contains(request.defaultOrientation)
        || request.defaultJustification < 0 || request.defaultJustification > 2)
        return std::unexpected(QStringLiteral("Недопустимые параметры этикетки."));
    auto next = document;
    bool changed = false;
    changed |= assign(next.width, request.width);
    changed |= assign(next.height, request.height);
    changed |= assign(next.dpi, request.dpi);
    changed |= assign(next.defaultOrientation, request.defaultOrientation);
    changed |= assign(next.defaultJustification, request.defaultJustification);
    changed |= assign(next.printMirror, request.printMirror);
    changed |= assign(next.printInverted, request.printInverted);
    changed |= assign(next.labelReverse, request.labelReverse);
    for (auto& element : next.elements) {
        if (element.type != ElementType::Eac) continue;
        const int size = eacSizeForRequest(element.width, next.dpi, element.eacStyle);
        changed |= assign(element.width, size);
        changed |= assign(element.height, size);
    }
    if (changed) {
        next.originalZpl.clear();
        document = std::move(next);
    }
    return changed;
}

OperationResult applyOperation(DesignDocument& document, const AddElement& request)
{
    if (!document.visualEditable)
        return std::unexpected(QStringLiteral("Добавление доступно только в визуальном режиме."));
    if (document.elements.size() >= 200)
        return std::unexpected(QStringLiteral("На этикетке уже 200 элементов — предел текущей версии."));
    auto element = request.element;
    if (element.id.isNull() || std::ranges::any_of(document.elements, [&](const Element& existing) {
        return existing.id == element.id;
    })) return std::unexpected(QStringLiteral("Идентификатор нового элемента должен быть уникальным."));
    if (static_cast<unsigned>(element.type) > static_cast<unsigned>(ElementType::Eac)
        || element.width < 1 || element.width > 4096 || element.height < 1 || element.height > 4096
        || element.x < -4096 || element.x > 4096 || element.y < -4096 || element.y > 4096)
        return std::unexpected(QStringLiteral("Недопустимый тип или геометрия нового элемента."));
    if (element.type == ElementType::Eac) {
        if (static_cast<unsigned>(element.eacStyle) > static_cast<unsigned>(EacStyle::WhiteTransparent)
            || element.orientation != u'N' || element.reverse)
            return std::unexpected(QStringLiteral("Недопустимые параметры знака ЕАС."));
        element.width = element.height = eacSizeForRequest(std::max(element.width, element.height), document.dpi, element.eacStyle);
    }
    document.elements.append(element);
    document.originalZpl.clear();
    return true;
}

OperationResult applyOperation(DesignDocument& document, const RenameElement& request)
{
    const auto index = editableIndex(document, request.id);
    if (!index) return std::unexpected(index.error());
    const auto name = request.name.trimmed();
    if (name.size() > 256)
        return std::unexpected(QStringLiteral("Название слоя — не более 256 символов."));
    auto element = document.elements[*index];
    const bool changed = assign(element.name, name);
    return replaceChanged(document, *index, std::move(element), changed);
}

OperationResult applyOperation(DesignDocument& document, const SetElementFlags& request)
{
    if (request.flag != LayerFlag::Locked && request.flag != LayerFlag::Visible && request.flag != LayerFlag::Printable)
        return std::unexpected(QStringLiteral("Неизвестное свойство слоя."));
    QList<qsizetype> indices;
    for (const auto& id : request.ids) {
        const auto index = editableIndex(document, id, request.flag != LayerFlag::Printable);
        if (!index) return std::unexpected(index.error());
        indices.append(*index);
    }
    auto next = document;
    bool changed = false;
    for (const auto index : indices) {
        auto& element = next.elements[index];
        switch (request.flag) {
        case LayerFlag::Locked: changed |= assign(element.locked, request.value); break;
        case LayerFlag::Visible: changed |= assign(element.editorVisible, request.value); break;
        case LayerFlag::Printable: changed |= assign(element.printable, request.value); break;
        }
    }
    if (changed) {
        next.originalZpl.clear();
        document = std::move(next);
    }
    return changed;
}

OperationResult applyOperation(DesignDocument& document, const ReorderElements& request)
{
    QSet<QUuid> selectedIds;
    for (const auto& id : request.ids) {
        const auto index = editableIndex(document, id);
        if (!index) return std::unexpected(index.error());
        selectedIds.insert(id);
    }
    if (selectedIds.isEmpty() || request.direction == 0) return false;
    auto next = document;
    if (request.direction >= 200 || request.direction <= -200) {
        QList<Element> selected, other;
        for (const auto& element : next.elements)
            (selectedIds.contains(element.id) ? selected : other).append(element);
        next.elements = request.direction > 0 ? other + selected : selected + other;
    } else if (request.direction > 0) {
        for (qsizetype i = next.elements.size() - 2; i >= 0; --i)
            if (selectedIds.contains(next.elements[i].id) && !selectedIds.contains(next.elements[i + 1].id))
                next.elements.swapItemsAt(i, i + 1);
    } else {
        for (qsizetype i = 1; i < next.elements.size(); ++i)
            if (selectedIds.contains(next.elements[i].id) && !selectedIds.contains(next.elements[i - 1].id))
                next.elements.swapItemsAt(i, i - 1);
    }
    if (std::ranges::equal(next.elements, document.elements, {}, &Element::id, &Element::id)) return false;
    next.originalZpl.clear();
    document = std::move(next);
    return true;
}

} // namespace Designer
