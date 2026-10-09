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

QLabel* caption(const QString& text, QWidget* parent = nullptr, int size = 10);
QLabel* heading(const QString& text, QWidget* parent = nullptr);
QLabel* sectionHeading(const QString& text, QWidget* parent);
QWidget* propertyField(const QString& text, QWidget* control, QWidget* parent);
void setPropertyRowVisible(QFormLayout* form, QWidget* control, bool visible);
QSpinBox* spin(const QString& name, int minimum, int maximum, QWidget* parent);
QSpinBox* dotSpin(const QString& name, int minimum, int maximum, QWidget* parent);

} // namespace Designer
