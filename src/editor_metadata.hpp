#pragma once
#include "design_document.hpp"
namespace Designer {
QString writeEditorMetadata(const DesignDocument& document, QString source);
bool restoreEditorMetadata(DesignDocument& document, const QString& source);
}
