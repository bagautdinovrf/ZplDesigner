#pragma once

namespace Designer {

// Installs the bundled Russian Qt translations before constructing dialogs.
// The application owns the translator for its entire lifetime.
[[nodiscard]] bool installRussianUiTranslations();

} // namespace Designer
