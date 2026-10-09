#pragma once

#include "canvas_layout.hpp"
#include "document_operations.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace Designer {
class DocumentSession;
class PreviewCoordinator;
class ElementSelection;
class SelectionProperties;

// Widget values are translated to typed edit requests. Session rules and undo
// stay outside this panel; element identity is always taken from shared UUIDs.
class PropertiesPanel final : public QWidget {
    Q_OBJECT
public:
    PropertiesPanel(DocumentSession* session, PreviewCoordinator* previews,
                    ElementSelection* selection, QWidget* parent = nullptr);
    void refresh();
    void refreshUnits();
    void focusContent();

signals:
    void editRequested(Designer::EditOperation operation, const QString& title);
    void resizeRequested(QSize size);
    void unitsRequested(Designer::CanvasUnit unit);
    void noticeRequested(const QString& message);
    void alignmentRequested(const QString& actionName);
    void imageRequested();
    void tableRequested();
    void qrRequested();
    void barcodeRequested();
    void textRequested();
    void gs1Requested();
    void valuesRequested();
    void duplicateRequested();
    void deleteRequested();

private:
    void editBarcodeType();
    void editProperties();
    [[nodiscard]] QList<int> selectedIndices() const;
    [[nodiscard]] int selectedIndex() const;
    [[nodiscard]] bool selectionLocked() const;
    [[nodiscard]] QSize elementSize(int index) const;

    DocumentSession* session_;
    PreviewCoordinator* previews_;
    ElementSelection* selection_;
    int selected_ = -1;
    bool updating_ = false;
    QLabel* objectSubtitle_ = nullptr;
    QLabel* objectSymbol_ = nullptr;
    QWidget* contentSection_ = nullptr;
    SelectionProperties* selectionProperties_ = nullptr;
    QPlainTextEdit* content_ = nullptr;
    QLabel* typeTitle_ = nullptr;
    QLabel* sizeHint_ = nullptr;
    QWidget* elementForm_ = nullptr;
    QSpinBox* x_ = nullptr;
    QSpinBox* y_ = nullptr;
    QSpinBox* width_ = nullptr;
    QSpinBox* height_ = nullptr;
    QSpinBox* thickness_ = nullptr;
    QSpinBox* fontHeight_ = nullptr;
    QSpinBox* fontWidth_ = nullptr;
    QSpinBox* blockWidth_ = nullptr;
    QSpinBox* blockLines_ = nullptr;
    QSpinBox* blockSpacing_ = nullptr;
    QSpinBox* blockIndent_ = nullptr;
    QComboBox* blockAlignment_ = nullptr;
    QComboBox* code128Mode_ = nullptr;
    QComboBox* barcodeType_ = nullptr;
    QComboBox* eacStyle_ = nullptr;
    QLabel* eacHint_ = nullptr;
    QComboBox* propertyUnits_ = nullptr;
    QComboBox* qrCorrection_ = nullptr;
    QSpinBox* pdfRowHeight_ = nullptr;
    QSpinBox* pdfColumns_ = nullptr;
    QSpinBox* pdfSecurity_ = nullptr;
    QPushButton* qrPayload_ = nullptr;
    QPushButton* tableEdit_ = nullptr;
    QPushButton* barcodeOptions_ = nullptr;
    QPushButton* advancedText_ = nullptr;
    QCheckBox* whiteGraphic_ = nullptr;
    QComboBox* diagonalDirection_ = nullptr;
    QCheckBox* interpretationAbove_ = nullptr;
    QSpinBox* module_ = nullptr;
    QSpinBox* threshold_ = nullptr;
    QComboBox* orientation_ = nullptr;
    QCheckBox* dither_ = nullptr;
    QCheckBox* imageKeepAspectRatio_ = nullptr;
    QCheckBox* geometryAspect_ = nullptr;
    QPushButton* imageEdit_ = nullptr;
    QCheckBox* reverse_ = nullptr;
    QCheckBox* readable_ = nullptr;
    QCheckBox* gs1_ = nullptr;
    QPushButton* gs1Builder_ = nullptr;
    QPushButton* placeholderValues_ = nullptr;
    QLabel* gs1Status_ = nullptr;
};

} // namespace Designer
