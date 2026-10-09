#pragma once

#include "design_document.hpp"

namespace Designer {
// Shared by Text, Code128, QR and DataMatrix; the original function names are
// retained for existing callers of the core API.
std::expected<QStringList, QString> dataMatrixParameters(const DesignDocument& document);
// Expands once into a temporary document. GS1 values are validated after
// substitution, field by field; they cannot inject additional AI boundaries.
std::expected<DesignDocument, QString> resolveDataMatrixValues(const DesignDocument& document,
                                                             bool allowUnresolvedText = false);
bool supportsValueParameters(ElementType type);
} // namespace Designer
