#pragma once

#include "design_document.hpp"

#include <QWidget>

class QLabel;
class QComboBox;
class QPlainTextEdit;
class QPushButton;

namespace Designer {
class PreviewView;

// Displays the raster produced by QtZpl without editor decorations.
class PreviewWidget final : public QWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    void setPreview(const Preview& preview, int dpi, const QString& error = {});
    void setPages(int count, int current = 0);
    [[nodiscard]] int pageCount() const;
    [[nodiscard]] int currentPage() const;

signals:
    void exportRequested();
    void pageRequested(int pageIndex);

private:
    void updateZoomControls();

    PreviewView* view_ = nullptr;
    QPushButton* fit_ = nullptr;
    QPushButton* actualSize_ = nullptr;
    QPushButton* zoomIn_ = nullptr;
    QPushButton* zoomOut_ = nullptr;
    QPushButton* export_ = nullptr;
    QLabel* zoom_ = nullptr;
    QLabel* dimensions_ = nullptr;
    QLabel* status_ = nullptr;
    QPlainTextEdit* diagnostics_ = nullptr;
    QWidget* pageControls_ = nullptr;
    QComboBox* pages_ = nullptr;
};

} // namespace Designer
