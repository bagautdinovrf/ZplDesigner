#pragma once
#include "design_document.hpp"

namespace Designer {
inline constexpr auto elementMimeType = "application/x-qtzpl-designer-elements-v1";
QByteArray encodeElements(const QList<Element>& elements);
// Before 1.10.0 a Codabar element kept its start and stop characters in the
// text and was written as ^BK without k and l, which printers read as A.
// Projects and the clipboard restore the characters the user saw; ZPL
// metadata snapshots describe the printed body, so they keep the text.
enum class LegacyCodabar { FromText, AsPrinted };
std::expected<QList<Element>, QString> decodeElements(const QByteArray& data,
                                                      LegacyCodabar legacy = LegacyCodabar::FromText);
} // namespace Designer
