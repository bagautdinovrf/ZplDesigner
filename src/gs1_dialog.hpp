#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRegularExpressionValidator;
class QTableWidget;

namespace Designer {
struct Gs1Data;

// Edits the GS1 element string; the document session remains responsible for
// applying the accepted value and recording its undo command.
class Gs1Dialog final : public QDialog {
    Q_OBJECT

public:
    explicit Gs1Dialog(QString input, QWidget* parent = nullptr);
    [[nodiscard]] QString data() const { return data_; }

public slots:
    void accept() override;

private:
    void readSource();
    void readFields();
    void showValidData(const Gs1Data& data);
    void showError(const QString& error);
    void updateRowRules();
    void updateActions();
    void addField();
    void removeField();
    void moveField(int delta);
    void insertVariable();
    void insertAiVariable();
    void updateVariableSyntax();

    QTableWidget* fields_ = nullptr;
    QComboBox* identifiers_ = nullptr;
    QLineEdit* variableName_ = nullptr;
    QComboBox* variableSyntax_ = nullptr;
    QRegularExpressionValidator* variableValidator_ = nullptr;
    QPushButton* insertVariable_ = nullptr;
    QPushButton* insertAiVariable_ = nullptr;
    QPlainTextEdit* source_ = nullptr;
    QPlainTextEdit* encoded_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* up_ = nullptr;
    QPushButton* down_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QString data_;
    bool valid_ = false;
};

} // namespace Designer
