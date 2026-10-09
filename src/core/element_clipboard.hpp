#pragma once
#include "design_document.hpp"

namespace Designer {
inline constexpr auto elementMimeType = "application/x-qtzpl-designer-elements-v1";
QByteArray encodeElements(const QList<Element>& elements);
std::expected<QList<Element>, QString> decodeElements(const QByteArray& data);
} // namespace Designer
