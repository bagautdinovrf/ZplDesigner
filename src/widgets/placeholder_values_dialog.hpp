#pragma once

#include "design_document.hpp"

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QTableWidget;

namespace Designer {

class PlaceholderValuesDialog final : public QDialog {
    Q_OBJECT

public:
    explicit PlaceholderValuesDialog(const DesignDocument& document, QWidget* parent = nullptr);
    [[nodiscard]] QMap<QString, QString> values() const { return values_; }

public slots:
    void accept() override;

private:
    void updateValues();

    DesignDocument document_;
    QMap<QString, QString> values_;
    QStringList parameters_;
    QString parameterError_;
    QTableWidget* table_ = nullptr;
    QLabel* status_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    bool valid_ = false;
};

} // namespace Designer
