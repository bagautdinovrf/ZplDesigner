#pragma once

#include "design_document.hpp"

#include <QIcon>

namespace Designer {
[[nodiscard]] QIcon elementIcon(ElementType type);
[[nodiscard]] QString elementSymbol(ElementType type);
} // namespace Designer
