#pragma once

#include <QLabel>
#include <QWidget>

class QFormLayout;
class QSpinBox;

namespace Designer {

class WorkspaceSurface final : public QWidget {
public:
    explicit WorkspaceSurface(QWidget* parent = nullptr, bool alternate = false,
                              bool rounded = false, bool bottomSeparator = true);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    bool alternate_;
    bool rounded_;
    bool bottomSeparator_;
};

class ObjectBadge final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent* event) override;
};

// Each line elides to the available width, so long names never widen a panel.
class ElidedLabel final : public QLabel {
public:
    using QLabel::QLabel;
    QSize minimumSizeHint() const override;
protected:
    void paintEvent(QPaintEvent*) override;
};

// A muted tag in an 18 px pill: a count ("6") or a scope ("все этикетки").
class PillBadge final : public QWidget {
public:
    explicit PillBadge(const QString& text = {}, QWidget* parent = nullptr);
    void setText(const QString& text);
    [[nodiscard]] QString text() const { return text_; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QString text_;
};

// One-pixel border-coloured rule across a panel.
class PanelSeparator final : public QWidget {
public:
    explicit PanelSeparator(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
};

// Concept sizes such as 10.5 px fall between whole pixels; those use points
// at the primary screen's logical resolution (96 dpi with Qt 6 scaling).
void setFontPixels(QFont& font, qreal pixels);

QLabel* caption(const QString& text, QWidget* parent = nullptr, int size = 10);
QLabel* heading(const QString& text, QWidget* parent = nullptr, bool elide = false);
QLabel* sectionHeading(const QString& text, QWidget* parent);
QWidget* propertyField(const QString& text, QWidget* control, QWidget* parent);
void setPropertyRowVisible(QFormLayout* form, QWidget* control, bool visible);
QSpinBox* spin(const QString& name, int minimum, int maximum, QWidget* parent);
QSpinBox* dotSpin(const QString& name, int minimum, int maximum, QWidget* parent);

} // namespace Designer
