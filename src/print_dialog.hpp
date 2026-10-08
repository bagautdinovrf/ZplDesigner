#pragma once

#include "design_document.hpp"
#include "printer_profiles.hpp"

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace Designer {
class PrintDialog final : public QDialog {
    Q_OBJECT
public:
    explicit PrintDialog(const DesignDocument& document, QWidget* parent = nullptr);
    void reject() override;

private:
    void refreshOptions();
    void submit();
    void loadPreferences();
    void loadProfiles();
    void refreshProfiles(const QString& selectName = {});
    void selectProfile();
    void saveProfile();
    void renameProfile();
    void deleteProfile();
    void editPrinterParameters();
    [[nodiscard]] PrinterProfile currentProfile() const;

    const DesignDocument document_;
    qint64 rawLabelsPerRepeat_ = 1;
    bool busy_ = false;
    QFormLayout* form_ = nullptr;
    QWidget* controls_ = nullptr;
    QComboBox* profiles_ = nullptr;
    QComboBox* dpi_ = nullptr;
    QLabel* dimensions_ = nullptr;
    QLabel* parametersSummary_ = nullptr;
    QList<PrinterProfile> catalogue_;
    QStringList availablePrinterNames_;
    PrinterParameters parameters_;
    int timeoutMs_ = 10000;
    bool catalogueWritable_ = true;
    QComboBox* destination_ = nullptr;
    QComboBox* printer_ = nullptr;
    QComboBox* mode_ = nullptr;
    QComboBox* paper_ = nullptr;
    QLineEdit* host_ = nullptr;
    QSpinBox* port_ = nullptr;
    QSpinBox* repeats_ = nullptr;
    QLabel* explanation_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* print_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
} // namespace Designer
