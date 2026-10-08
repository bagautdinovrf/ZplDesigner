#pragma once

#include "canvas_layout.hpp"
#include "design_document.hpp"

#include <optional>

namespace Designer {

inline constexpr qint64 maximumProjectBytes = 64 * 1024 * 1024;

// Portable editing state. Preview inputs intentionally remain session-only.
// Pending text can be invalid ZPL; retain the last successfully applied document.
struct ProjectState {
    DesignDocument document;
    CanvasLayoutSettings layout;
    std::optional<QString> pendingSource;
};

[[nodiscard]] std::expected<QByteArray, QString> encodeProject(const ProjectState& project);
[[nodiscard]] std::expected<ProjectState, QString> decodeProject(const QByteArray& bytes);

} // namespace Designer
