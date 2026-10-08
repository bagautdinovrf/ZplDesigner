#pragma once
#include "design_document.hpp"
#include <QDialog>

namespace Designer {
class LabelSettingsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit LabelSettingsDialog(const DesignDocument& document, QWidget* parent = nullptr);
    DesignDocument editedDocument() const;
private:
    DesignDocument document_;
};
}
