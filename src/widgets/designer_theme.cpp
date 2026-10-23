#include "designer_theme.hpp"

#include <QApplication>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QFont>
#include <QFontMetrics>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <array>

namespace Designer {
namespace {
QPointer<QStyle> installedStyle;
DesignerTheme currentTheme = DesignerTheme::Light;

QColor colorFor(DesignerColor color, DesignerTheme theme)
{
    // Exact workspace.css tokens, including alpha in grid and shadow colors.
    // Danger and the ruler pair come from the canvas-guides concept tokens.
    static constexpr std::array<QRgb, 17> light{
        0xfff1f3f5, 0xffffffff, 0xfff8f9fb, 0xff253140, 0xff6a7583,
        0xffdce1e7, 0xff246bdd, 0xffe9f1ff, 0xffe6e9ee, 0x66aeb9c7,
        0x1227384f, 0xff2c7961, 0xff8b6409, 0xffffffff, 0xffb42f2f,
        0xffffffff, 0xff7b8592};
    static constexpr std::array<QRgb, 17> dark{
        0xff161b22, 0xff242b34, 0xff20262e, 0xffe0e6ee, 0xffa7b3c2,
        0xff3a4452, 0xff85b1ff, 0xff293d5a, 0xff171e27, 0x66627187,
        0x44000000, 0xff83cbb3, 0xffefc96d, 0xff1d242d, 0xffff9d9d,
        0xff20262e, 0xff97a3b2};
    return QColor::fromRgba((theme == DesignerTheme::Dark ? dark : light)[static_cast<std::size_t>(color)]);
}

bool compact(const QWidget* widget)
{
    return widget && (widget->property("compact").toBool()
        || widget->property("designerControl").toString() == QStringLiteral("mini"));
}

bool variant(const QWidget* widget, const char* name)
{
    return widget && widget->property("designerControl").toString() == QLatin1String(name);
}

bool embeddedEditor(const QWidget* widget)
{
    if (!widget) return false;
    const auto* parent = widget->parentWidget();
    return qobject_cast<const QAbstractSpinBox*>(parent) || qobject_cast<const QComboBox*>(parent);
}

bool inspectorTabBar(const QWidget* widget)
{
    return widget && widget->parentWidget()
        && widget->parentWidget()->objectName() == QStringLiteral("inspectorTabs");
}

void roundedRect(QPainter* painter, const QRect& rect, const QColor& fill,
                 const QColor& border = Qt::transparent, qreal radius = 4)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(border.alpha() ? QPen(border, 1) : QPen(Qt::NoPen));
    painter->setBrush(fill);
    painter->drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter->restore();
}

void chevron(QPainter* painter, const QRect& rect, Qt::ArrowType arrow, const QColor& color)
{
    QPolygonF points;
    if (arrow == Qt::UpArrow) points = {{-3, 1.5}, {0, -1.5}, {3, 1.5}};
    else if (arrow == Qt::DownArrow) points = {{-3, -1.5}, {0, 1.5}, {3, -1.5}};
    else if (arrow == Qt::LeftArrow) points = {{1.5, -3}, {-1.5, 0}, {1.5, 3}};
    else points = {{-1.5, -3}, {1.5, 0}, {-1.5, 3}};
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(QRectF(rect).center());
    painter->setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPolyline(points);
    painter->restore();
}

class DesignerStyle final : public QProxyStyle {
public:
    DesignerStyle() : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))) {}

    void polish(QWidget* widget) override
    {
        QProxyStyle::polish(widget);
        if (qobject_cast<QMenu*>(widget)) {
            // Windows needs a frameless popup for per-pixel transparent corners.
            widget->setWindowFlag(Qt::FramelessWindowHint);
            widget->setAttribute(Qt::WA_TranslucentBackground);
        }
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const override
    {
        switch (metric) {
        case PM_DefaultFrameWidth: case PM_SpinBoxFrameWidth: case PM_ComboBoxFrameWidth:
        case PM_MenuPanelWidth: case PM_SplitterWidth: return 1;
        case PM_MenuHMargin: return 7;
        case PM_MenuVMargin: return 4;
        case PM_ButtonMargin: return compact(widget) ? 7 : 9;
        case PM_ButtonDefaultIndicator: case PM_ButtonShiftHorizontal: case PM_ButtonShiftVertical:
        case PM_TabBarTabShiftHorizontal: case PM_TabBarTabShiftVertical:
        case PM_TabBarTabOverlap: case PM_TabBarBaseOverlap: case PM_TabBarBaseHeight:
        case PM_ToolBarFrameWidth: case PM_ToolBarItemMargin: case PM_MenuBarPanelWidth:
        case PM_FocusFrameHMargin: case PM_FocusFrameVMargin: return 0;
        case PM_ToolBarItemSpacing: return widget && widget->property("commandPanel").toBool() ? 0 : 6;
        case PM_ToolBarSeparatorExtent: return 11;
        case PM_ToolBarIconSize: case PM_ButtonIconSize: case PM_SmallIconSize: case PM_TabBarIconSize: return 16;
        case PM_IndicatorWidth: case PM_IndicatorHeight: case PM_ExclusiveIndicatorWidth: case PM_ExclusiveIndicatorHeight: return 13;
        case PM_CheckBoxLabelSpacing: case PM_RadioButtonLabelSpacing: return 7;
        case PM_ScrollBarExtent: return 8;
        case PM_ScrollBarSliderMin: return 28;
        case PM_LayoutHorizontalSpacing: return 10;
        case PM_LayoutVerticalSpacing: return 8;
        case PM_TabBarTabHSpace: return inspectorTabBar(widget) ? 12 : 26;
        case PM_TabBarTabVSpace: return 16;
        default: return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* result) const override
    {
        switch (hint) {
        case SH_EtchDisabledText: case SH_DitherDisabledText: case SH_UnderlineShortcut:
        case SH_ItemView_ActivateItemOnSingleClick: case SH_Widget_Animate: case SH_ComboBox_Popup: return 0;
        case SH_TabBar_Alignment: return Qt::AlignLeft;
        case SH_Menu_SupportsSections: case SH_Menu_Scrollable: return 1;
        default: return QProxyStyle::styleHint(hint, option, widget, result);
        }
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption* option,
                           const QSize& contents, const QWidget* widget) const override
    {
        const bool small = compact(widget);
        const int fieldHeight = std::max(option->fontMetrics.height() + (small ? 8 : 12), small ? 25 : 30);
        switch (type) {
        case CT_PushButton: case CT_ToolButton:
            return {contents.width() + (small ? 14 : 18), std::max(contents.height() + 8, small ? 25 : 29)};
        case CT_LineEdit:
            return embeddedEditor(widget) ? QSize(contents.width(), option->fontMetrics.height() + 4)
                                          : QSize(contents.width() + 16, fieldHeight);
        case CT_SpinBox: case CT_ComboBox: return {contents.width() + 28, fieldHeight};
        case CT_TabBarTab: {
            const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
            if (!tab) return {contents.width(), 37};
            QFont font = widget ? widget->font() : QApplication::font();
            if (inspectorTabBar(widget)) font.setPixelSize(11);
            const int textWidth = QFontMetrics(font).horizontalAdvance(tab->text);
            const int iconWidth = tab->icon.isNull() ? 0 : tab->iconSize.width() + 6;
            const int buttonsWidth = std::max(0, tab->leftButtonSize.width()) + std::max(0, tab->rightButtonSize.width());
            return {textWidth + iconWidth + buttonsWidth + (inspectorTabBar(widget) ? 12 : 26), 37};
        }
        case CT_MenuBarItem: return {contents.width() + 18, 29};
        case CT_MenuItem: {
            const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
            if (!item) break;
            if (item->menuItemType == QStyleOptionMenuItem::Separator) {
                QFont sectionFont = item->font;
                sectionFont.setPixelSize(11);
                const int height = item->text.isEmpty() ? 5
                    : std::max(22, QFontMetrics(sectionFont).height() + 6);
                return {contents.width() + 20, height};
            }
            if (item->menuItemType == QStyleOptionMenuItem::Scroller
                || item->menuItemType == QStyleOptionMenuItem::TearOff
                || item->menuItemType == QStyleOptionMenuItem::EmptyArea) break;
            const int checkWidth = item->menuHasCheckableItems ? 20 : 0;
            const int iconWidth = item->maxIconWidth > 0 ? item->maxIconWidth + 8 : 0;
            // QMenu adds the widest shortcut after measuring all action labels.
            return {contents.width() + checkWidth + iconWidth + 44,
                    std::max({25, contents.height() + 6, item->fontMetrics.height() + 6})};
        }
        default: return QProxyStyle::sizeFromContents(type, option, contents, widget);
        }
        return QProxyStyle::sizeFromContents(type, option, contents, widget);
    }

    QRect subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const override
    {
        if (element == SE_ItemViewItemDecoration || element == SE_ItemViewItemFocusRect) {
            const auto* item = qstyleoption_cast<const QStyleOptionViewItem*>(option);
            if (item && item->features.testFlag(QStyleOptionViewItem::HasDecoration)
                && !item->features.testFlag(QStyleOptionViewItem::HasDisplay)
                && !item->features.testFlag(QStyleOptionViewItem::HasCheckIndicator)) {
                // Icon-only items have no caption area to lay out or focus below the icon.
                return element == SE_ItemViewItemDecoration
                    ? alignedRect(item->direction, Qt::AlignCenter, item->decorationSize, item->rect)
                    : item->rect.adjusted(1, 1, -1, -1);
            }
        }
        if (element == SE_LineEditContents)
            return embeddedEditor(widget) ? option->rect : option->rect.adjusted(8, 1, -8, -1);
        if (element == SE_PushButtonContents) return option->rect.adjusted(8, 3, -8, -3);
        if (element == SE_PushButtonFocusRect) return option->rect.adjusted(2, 2, -2, -2);
        return QProxyStyle::subElementRect(element, option, widget);
    }

    SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                                    const QPoint& position, const QWidget* widget) const override
    {
        // Display-only editable combos need the native arrow handling across the
        // whole field, including Qt's popup release and mouse-replay safeguards.
        if (control == CC_ComboBox && widget && widget->property("popupOnWholeControl").toBool()
            && option->rect.contains(position))
            return SC_ComboBoxArrow;
        return QProxyStyle::hitTestComplexControl(control, option, position, widget);
    }

    QRect subControlRect(ComplexControl control, const QStyleOptionComplex* option,
                         SubControl subControl, const QWidget* widget) const override
    {
        const QRect rect = option->rect;
        if (control == CC_ComboBox) {
            if (subControl == SC_ComboBoxArrow)
                return visualRect(option->direction, rect, QRect(rect.right() - 22, rect.top() + 1, 22, rect.height() - 2));
            if (subControl == SC_ComboBoxEditField)
                return visualRect(option->direction, rect, rect.adjusted(8, 2, -25, -2));
            if (subControl == SC_ComboBoxFrame) return rect;
        }
        if (control == CC_SpinBox) {
            const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
            const bool arrows = spin && spin->buttonSymbols != QAbstractSpinBox::NoButtons;
            if (subControl == SC_SpinBoxEditField)
                return visualRect(option->direction, rect, rect.adjusted(8, 2, arrows ? -23 : -8, -2));
            if (subControl == SC_SpinBoxFrame) return rect;
            if (subControl == SC_SpinBoxUp || subControl == SC_SpinBoxDown) {
                if (!arrows) return {};
                const QRect button(rect.right() - 20, rect.top() + 1, 20, (rect.height() - 2) / 2);
                return visualRect(option->direction, rect, subControl == SC_SpinBoxUp ? button : button.translated(0, button.height()));
            }
        }
        if (control == CC_ToolButton) {
            const auto* tool = qstyleoption_cast<const QStyleOptionToolButton*>(option);
            const bool menu = tool && tool->features.testFlag(QStyleOptionToolButton::MenuButtonPopup);
            if (subControl == SC_ToolButton) return menu ? rect.adjusted(0, 0, -17, 0) : rect;
            if (subControl == SC_ToolButtonMenu) return menu ? QRect(rect.right() - 16, rect.top(), 17, rect.height()) : QRect{};
        }
        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget) const override
    {
        const QRect rect = option->rect;
        const QColor border = designerColor(DesignerColor::Border);
        switch (element) {
        case PE_PanelButtonCommand: case PE_PanelButtonBevel: case PE_PanelButtonTool:
            paintButton(option, painter, widget, element == PE_PanelButtonTool); return;
        case PE_FrameButtonBevel: case PE_FrameButtonTool: case PE_FrameDefaultButton:
        case PE_FrameStatusBarItem: case PE_IndicatorToolBarHandle: return;
        case PE_FrameFocusRect:
            if (option->state.testFlag(State_KeyboardFocusChange))
                roundedRect(painter, rect, Qt::transparent, designerColor(DesignerColor::Accent));
            return;
        case PE_FrameLineEdit: case PE_PanelLineEdit:
            if (embeddedEditor(widget)) {
                if (element == PE_PanelLineEdit) painter->fillRect(rect, designerColor(DesignerColor::Control));
                return;
            }
            roundedRect(painter, rect, designerColor(DesignerColor::Control),
                option->state.testFlag(State_HasFocus) ? designerColor(DesignerColor::Accent) : border); return;
        case PE_FrameMenu: return; // PE_PanelMenu paints the rounded border once.
        case PE_Frame: case PE_FrameDockWidget: case PE_FrameGroupBox:
            roundedRect(painter, rect, Qt::transparent, border); return;
        case PE_PanelMenu:
            roundedRect(painter, rect, designerColor(DesignerColor::Panel), border, 8); return;
        case PE_PanelTipLabel:
            roundedRect(painter, rect, designerColor(DesignerColor::Panel), border); return;
        case PE_FrameTabWidget:
            painter->fillRect(rect, designerColor(DesignerColor::Panel));
            painter->fillRect(QRect(rect.topLeft(), QSize(rect.width(), 1)), border); return;
        case PE_FrameTabBarBase:
            painter->fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), border); return;
        case PE_PanelToolBar: case PE_PanelMenuBar:
            if (widget && widget->property("commandPanel").toBool()) return;
            painter->fillRect(rect, designerColor(DesignerColor::Panel));
            painter->fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), border); return;
        case PE_PanelStatusBar:
            painter->fillRect(rect, designerColor(DesignerColor::PanelAlternate));
            painter->fillRect(QRect(rect.topLeft(), QSize(rect.width(), 1)), border); return;
        case PE_PanelScrollAreaCorner:
            painter->fillRect(rect, designerColor(DesignerColor::Panel)); return;
        case PE_IndicatorToolBarSeparator: {
            const bool horizontal = option->state.testFlag(State_Horizontal);
            painter->fillRect(horizontal ? QRect(rect.center().x(), rect.top() + 4, 1, std::max(0, rect.height() - 8))
                                         : QRect(rect.left() + 4, rect.center().y(), std::max(0, rect.width() - 8), 1), border);
            return;
        }
        case PE_IndicatorArrowDown: case PE_IndicatorSpinDown: case PE_IndicatorButtonDropDown:
            chevron(painter, rect, Qt::DownArrow, designerColor(DesignerColor::Muted)); return;
        case PE_IndicatorArrowUp: case PE_IndicatorSpinUp:
            chevron(painter, rect, Qt::UpArrow, designerColor(DesignerColor::Muted)); return;
        case PE_IndicatorArrowLeft:
            chevron(painter, rect, Qt::LeftArrow, designerColor(DesignerColor::Muted)); return;
        case PE_IndicatorArrowRight:
            chevron(painter, rect, Qt::RightArrow, designerColor(DesignerColor::Muted)); return;
        case PE_IndicatorCheckBox: case PE_IndicatorItemViewItemCheck: case PE_IndicatorRadioButton: {
            const bool checked = option->state.testFlag(State_On) || option->state.testFlag(State_NoChange);
            const QColor accent = designerColor(DesignerColor::Accent);
            painter->save();
            if (!option->state.testFlag(State_Enabled)) painter->setOpacity(painter->opacity() * 0.48);
            roundedRect(painter, rect, checked ? accent : designerColor(DesignerColor::Control), checked ? accent : border,
                        element == PE_IndicatorRadioButton ? 6.5 : 2);
            if (checked) {
                painter->setRenderHint(QPainter::Antialiasing);
                const QColor ink = currentTheme == DesignerTheme::Dark ? designerColor(DesignerColor::Control) : QColor(Qt::white);
                painter->setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                if (element == PE_IndicatorRadioButton) {
                    painter->setPen(Qt::NoPen); painter->setBrush(ink); painter->drawEllipse(QRectF(rect).adjusted(4, 4, -4, -4));
                } else if (option->state.testFlag(State_NoChange)) {
                    painter->drawLine(rect.left() + 3, rect.center().y(), rect.right() - 3, rect.center().y());
                } else {
                    const QPointF origin = rect.topLeft();
                    painter->drawPolyline(QPolygonF{origin + QPointF(3, 6.5), origin + QPointF(5.5, 9), origin + QPointF(10, 4)});
                }
            }
            painter->restore(); return;
        }
        case PE_PanelItemViewItem: case PE_PanelItemViewRow:
            if (option->state.testFlag(State_Selected) || option->state.testFlag(State_MouseOver))
                roundedRect(painter, rect.adjusted(1, 1, -1, -1), designerColor(DesignerColor::Tint));
            return;
        default: QProxyStyle::drawPrimitive(element, option, painter, widget); return;
        }
    }

    void drawControl(ControlElement element, const QStyleOption* option,
                     QPainter* painter, const QWidget* widget) const override
    {
        switch (element) {
        case CE_PushButton: {
            const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
            if (!button) break;
            paintButton(option, painter, widget, false);
            paintLabel(option, painter, widget, button->text, button->icon, button->iconSize,
                       Qt::ToolButtonTextBesideIcon, button->features.testFlag(QStyleOptionButton::HasMenu));
            return;
        }
        case CE_PushButtonBevel: paintButton(option, painter, widget, false); return;
        case CE_TabBarTab: case CE_TabBarTabShape: case CE_TabBarTabLabel: {
            const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
            if (!tab) break;
            const bool selected = option->state.testFlag(State_Selected);
            if (element != CE_TabBarTabLabel) {
                painter->fillRect(tab->rect, designerColor(DesignerColor::Panel));
                painter->fillRect(QRect(tab->rect.left(), tab->rect.bottom(), tab->rect.width(), 1), designerColor(DesignerColor::Border));
                if (selected) painter->fillRect(QRect(tab->rect.left(), tab->rect.bottom() - 1, tab->rect.width(), 2), designerColor(DesignerColor::Accent));
            }
            if (element != CE_TabBarTabShape) {
                painter->save();
                if (inspectorTabBar(widget)) { QFont font = painter->font(); font.setPixelSize(11); painter->setFont(font); }
                painter->setPen(designerColor(selected ? DesignerColor::Accent : DesignerColor::Muted));
                const int padding = inspectorTabBar(widget) ? 6 : 8;
                painter->drawText(tab->rect.adjusted(padding, 0, -padding, -1), Qt::AlignCenter | Qt::TextHideMnemonic, tab->text);
                painter->restore();
            }
            return;
        }
        case CE_ComboBoxLabel: {
            const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option);
            if (!combo || combo->editable) return;
            const QRect field = subControlRect(CC_ComboBox, combo, SC_ComboBoxEditField, widget);
            painter->save();
            if (compact(widget)) { QFont font = painter->font(); font.setPixelSize(11); painter->setFont(font); }
            painter->setPen(option->palette.color(option->state.testFlag(State_Enabled) ? QPalette::Active : QPalette::Disabled, QPalette::Text));
            painter->drawText(field, combo->textAlignment | Qt::TextSingleLine,
                painter->fontMetrics().elidedText(combo->currentText, Qt::ElideRight, field.width()));
            painter->restore(); return;
        }
        case CE_ToolButtonLabel: {
            const auto* tool = qstyleoption_cast<const QStyleOptionToolButton*>(option);
            if (!tool) break;
            QStyleOptionToolButton label(*tool);
            if (tool->features.testFlag(QStyleOptionToolButton::Arrow)) {
                if (tool->toolButtonStyle == Qt::ToolButtonIconOnly || tool->text.isEmpty()) {
                    chevron(painter, tool->rect, tool->arrowType, designerColor(DesignerColor::Muted));
                    return;
                }
                chevron(painter, QRect(tool->rect.left() + 6, tool->rect.top(), 16, tool->rect.height()),
                        tool->arrowType, designerColor(DesignerColor::Muted));
                label.rect.adjust(19, 0, 0, 0);
            }
            paintLabel(&label, painter, widget, tool->text, tool->icon, tool->iconSize,
                       tool->toolButtonStyle, tool->features.testFlag(QStyleOptionToolButton::HasMenu));
            return;
        }
        case CE_MenuBarItem: {
            const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
            if (!item) break;
            if (option->state.testFlag(State_Selected)) roundedRect(painter, option->rect, designerColor(DesignerColor::Tint));
            painter->save(); painter->setPen(designerColor(DesignerColor::Muted));
            painter->drawText(option->rect, Qt::AlignCenter | Qt::TextHideMnemonic, item->text);
            painter->restore(); return;
        }
        case CE_MenuBarEmptyArea: painter->fillRect(option->rect, designerColor(DesignerColor::Panel)); return;
        case CE_MenuItem: {
            const auto* item = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
            if (!item) break;
            if (item->menuItemType == QStyleOptionMenuItem::Scroller
                || item->menuItemType == QStyleOptionMenuItem::TearOff
                || item->menuItemType == QStyleOptionMenuItem::EmptyArea) break;
            paintMenuItem(item, painter);
            return;
        }
        case CE_ToolBar: drawPrimitive(PE_PanelToolBar, option, painter, widget); return;
        case CE_Splitter: painter->fillRect(option->rect, designerColor(DesignerColor::Border)); return;
        case CE_HeaderSection: case CE_HeaderEmptyArea:
            painter->fillRect(option->rect, designerColor(DesignerColor::PanelAlternate));
            painter->fillRect(QRect(option->rect.left(), option->rect.bottom(), option->rect.width(), 1), designerColor(DesignerColor::Border)); return;
        case CE_ScrollBarAddLine: case CE_ScrollBarSubLine: case CE_ScrollBarAddPage: case CE_ScrollBarSubPage: return;
        case CE_ScrollBarSlider:
            roundedRect(painter, option->rect.adjusted(1, 1, -1, -1), designerColor(option->state.testFlag(State_MouseOver) ? DesignerColor::Muted : DesignerColor::Border), Qt::transparent, 3); return;
        default: break;
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                            QPainter* painter, const QWidget* widget) const override
    {
        if (control == CC_ComboBox || control == CC_SpinBox) {
            roundedRect(painter, option->rect, designerColor(DesignerColor::Control),
                designerColor(option->state.testFlag(State_HasFocus) ? DesignerColor::Accent : DesignerColor::Border));
            if (control == CC_ComboBox) {
                chevron(painter, subControlRect(control, option, SC_ComboBoxArrow, widget), Qt::DownArrow, designerColor(DesignerColor::Muted));
            } else {
                const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
                if (spin && spin->buttonSymbols != QAbstractSpinBox::NoButtons) {
                    for (const auto sub : {SC_SpinBoxUp, SC_SpinBoxDown}) {
                        const auto flag = sub == SC_SpinBoxUp ? QAbstractSpinBox::StepUpEnabled : QAbstractSpinBox::StepDownEnabled;
                        QColor color = designerColor(DesignerColor::Muted);
                        if (!option->state.testFlag(State_Enabled) || !spin->stepEnabled.testFlag(flag)) color.setAlphaF(0.35f);
                        const QRect rect = subControlRect(control, option, sub, widget);
                        if (option->activeSubControls.testFlag(sub) && option->state.testFlag(State_MouseOver))
                            roundedRect(painter, rect, designerColor(DesignerColor::Tint), Qt::transparent, 2);
                        chevron(painter, rect, sub == SC_SpinBoxUp ? Qt::UpArrow : Qt::DownArrow, color);
                    }
                }
            }
            return;
        }
        if (control == CC_ToolButton) {
            const auto* tool = qstyleoption_cast<const QStyleOptionToolButton*>(option);
            if (!tool) return;
            paintButton(option, painter, widget, true);
            QStyleOptionToolButton label(*tool);
            label.rect = subControlRect(control, option, SC_ToolButton, widget);
            if (tool->features.testFlag(QStyleOptionToolButton::MenuButtonPopup)) {
                label.features &= ~QStyleOptionToolButton::HasMenu;
                chevron(painter, subControlRect(control, option, SC_ToolButtonMenu, widget), Qt::DownArrow, designerColor(DesignerColor::Muted));
            }
            drawControl(CE_ToolButtonLabel, &label, painter, widget);
            return;
        }
        if (control == CC_ScrollBar) {
            const QRect slider = QProxyStyle::subControlRect(control, option, SC_ScrollBarSlider, widget);
            roundedRect(painter, slider.adjusted(1, 1, -1, -1), designerColor(option->activeSubControls.testFlag(SC_ScrollBarSlider) ? DesignerColor::Muted : DesignerColor::Border), Qt::transparent, 3);
            return;
        }
        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }

private:
    void paintMenuItem(const QStyleOptionMenuItem* item, QPainter* painter) const
    {
        const QRect rect = item->rect;
        painter->save();
        painter->setFont(item->font);
        if (item->menuItemType == QStyleOptionMenuItem::Separator) {
            const bool section = !item->text.isEmpty();
            if (section) {
                QFont font = item->font;
                font.setPixelSize(11);
                font.setBold(false);
                painter->setFont(font);
                painter->setPen(designerColor(DesignerColor::Muted));
                painter->drawText(rect.adjusted(10, 0, -10, -3),
                    Qt::AlignVCenter | Qt::AlignLeading | Qt::TextHideMnemonic, item->text);
            }
            painter->setPen(designerColor(DesignerColor::Border));
            const int y = section ? rect.bottom() - 1 : rect.center().y();
            painter->drawLine(rect.left(), y, rect.right(), y);
            painter->restore();
            return;
        }

        const bool enabled = item->state.testFlag(State_Enabled);
        const bool selected = enabled && item->state.testFlag(State_Selected);
        if (selected)
            roundedRect(painter, rect.adjusted(0, 1, 0, -1), designerColor(DesignerColor::Tint));
        if (!enabled) painter->setOpacity(painter->opacity() * 0.48);
        const QColor ink = designerColor(selected ? DesignerColor::Accent : DesignerColor::Ink);
        const auto visual = [item, rect](const QRect& logical) {
            return QStyle::visualRect(item->direction, rect, logical);
        };
        QRect content = rect.adjusted(10, 0, -10, 0);
        if (item->menuHasCheckableItems) {
            const QRect check = visual(QRect(content.left(), content.center().y() - 7, 14, 14));
            if (item->checked) {
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                if (item->checkType == QStyleOptionMenuItem::Exclusive) {
                    painter->setBrush(ink);
                    painter->drawEllipse(QRectF(check).adjusted(4, 4, -4, -4));
                } else {
                    const QPointF origin = check.topLeft();
                    painter->drawPolyline(QPolygonF{origin + QPointF(2, 7), origin + QPointF(5, 10), origin + QPointF(12, 3)});
                }
            }
            content.adjust(20, 0, 0, 0);
        }
        if (item->maxIconWidth > 0) {
            const QRect iconRect = visual(QRect(content.left(), content.top(), item->maxIconWidth, content.height()));
            item->icon.paint(painter, iconRect, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled,
                             item->checked ? QIcon::On : QIcon::Off);
            content.adjust(item->maxIconWidth + 8, 0, 0, 0);
        }
        if (item->menuItemType == QStyleOptionMenuItem::SubMenu) {
            chevron(painter, visual(QRect(content.right() - 12, content.top(), 13, content.height())),
                    item->direction == Qt::RightToLeft ? Qt::LeftArrow : Qt::RightArrow,
                    selected ? ink : designerColor(DesignerColor::Muted));
        }
        const auto separator = item->text.indexOf(u'\t');
        const QString label = item->text.left(separator);
        const int shortcutWidth = item->reservedShortcutWidth;
        if (separator >= 0) {
            const QRect shortcut = visual(QRect(content.right() - shortcutWidth + 1, content.top(), shortcutWidth, content.height()));
            painter->setPen(designerColor(DesignerColor::Muted));
            painter->drawText(shortcut, Qt::AlignVCenter | Qt::AlignTrailing | Qt::TextSingleLine,
                              item->text.mid(separator + 1));
        }
        content.adjust(0, 0, -(shortcutWidth > 0 ? shortcutWidth + 24 : 16), 0);
        if (item->menuItemType == QStyleOptionMenuItem::DefaultItem) {
            QFont font = item->font; font.setBold(true); painter->setFont(font);
        }
        painter->setPen(ink);
        painter->drawText(visual(content), Qt::AlignVCenter | Qt::AlignLeading | Qt::TextHideMnemonic | Qt::TextSingleLine, label);
        painter->restore();
    }

    void paintButton(const QStyleOption* option, QPainter* painter, const QWidget* widget, bool tool) const
    {
        const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
        const bool flat = variant(widget, "flat") || (widget && widget->property("flat").toBool())
            || (button && button->features.testFlag(QStyleOptionButton::Flat))
            || (tool && option->state.testFlag(State_AutoRaise));
        const bool checked = option->state.testFlag(State_On);
        const bool hovered = option->state.testFlag(State_MouseOver) || option->state.testFlag(State_Sunken);
        const bool accent = variant(widget, "accent");
        const bool segment = variant(widget, "segment");
        // One option of a segmented choice: only the active option is tinted.
        const bool choice = variant(widget, "choice");
        QColor fill = flat || segment || choice ? QColor(Qt::transparent) : designerColor(DesignerColor::Control);
        QColor border = flat || segment || choice || accent ? QColor(Qt::transparent) : designerColor(DesignerColor::Border);
        if ((hovered && !choice) || checked || accent) fill = designerColor(DesignerColor::Tint);
        if (checked && !flat && !segment && !choice) border = designerColor(DesignerColor::Accent);
        if (segment && checked) fill = designerColor(DesignerColor::Panel);
        if (option->state.testFlag(State_HasFocus) && option->state.testFlag(State_KeyboardFocusChange)) border = designerColor(DesignerColor::Accent);
        painter->save();
        if (!option->state.testFlag(State_Enabled)) painter->setOpacity(painter->opacity() * 0.48);
        roundedRect(painter, option->rect, fill, border, choice ? 3 : 4);
        painter->restore();
    }

    void paintLabel(const QStyleOption* option, QPainter* painter, const QWidget* widget,
                    const QString& text, const QIcon& icon, const QSize& iconSize,
                    Qt::ToolButtonStyle presentation, bool menu) const
    {
        const bool enabled = option->state.testFlag(State_Enabled);
        const bool selected = option->state.testFlag(State_On) || variant(widget, "accent");
        const bool choice = variant(widget, "choice");
        painter->save();
        if (!enabled) painter->setOpacity(painter->opacity() * 0.48);
        if (compact(widget)) { QFont font = painter->font(); font.setPixelSize(11); painter->setFont(font); }
        if (choice && selected) { QFont font = painter->font(); font.setWeight(QFont::DemiBold); painter->setFont(font); }
        // A tone recolours the caption of a quiet command; its icon brings its own colour.
        const QString tone = widget ? widget->property("tone").toString() : QString();
        const DesignerColor idle = choice || tone == QLatin1String("muted") ? DesignerColor::Muted
            : tone == QLatin1String("danger") ? DesignerColor::Danger : DesignerColor::Ink;
        painter->setPen(designerColor(selected ? DesignerColor::Accent : idle));
        const int inset = widget && widget->property("commandButton").toBool() ? 4 : compact(widget) ? 7 : 9;
        QRect content = option->rect.adjusted(inset, 2, -inset, -2);
        if (menu) {
            chevron(painter, QRect(content.right() - 10, content.top(), 11, content.height()), Qt::DownArrow, designerColor(DesignerColor::Muted));
            content.adjust(0, 0, -15, 0);
        }
        const bool showIcon = !icon.isNull() && presentation != Qt::ToolButtonTextOnly;
        const bool showText = !text.isEmpty() && (presentation != Qt::ToolButtonIconOnly || !showIcon);
        // A lone icon has no caption to inset from: it may use the whole button.
        const QRect iconArea = showIcon && !showText && !menu ? option->rect.adjusted(2, 2, -2, -2) : content;
        const QSize paintedIcon = iconSize.isValid() ? iconSize.boundedTo(iconArea.size()) : QSize(16, 16);
        // QRect::center() rounds down, which put icons a pixel above and left of
        // the middle of even-sized buttons; centre by the free space instead.
        const auto centredX = [](const QRect& area, int width) { return area.left() + (area.width() - width) / 2; };
        const int iconY = iconArea.top() + (iconArea.height() - paintedIcon.height()) / 2;
        const auto mode = !enabled ? QIcon::Disabled : selected ? QIcon::Selected : QIcon::Normal;
        if (showIcon && !showText) {
            const QRect iconRect(centredX(iconArea, paintedIcon.width()), iconY, paintedIcon.width(), paintedIcon.height());
            icon.paint(painter, iconRect, Qt::AlignCenter, mode);
        } else if (showIcon && presentation == Qt::ToolButtonTextUnderIcon) {
            const QRect iconRect(centredX(content, paintedIcon.width()), content.top(), paintedIcon.width(), paintedIcon.height());
            icon.paint(painter, iconRect, Qt::AlignCenter, mode);
            painter->drawText(content.adjusted(0, paintedIcon.height() + 3, 0, 0), Qt::AlignCenter | Qt::TextHideMnemonic, text);
        } else if (showIcon) {
            const int total = std::min(content.width(), paintedIcon.width() + 6 + painter->fontMetrics().horizontalAdvance(text));
            const int start = centredX(content, total);
            const QRect iconRect(start, iconY, paintedIcon.width(), paintedIcon.height());
            icon.paint(painter, iconRect, Qt::AlignCenter, mode);
            painter->drawText(QRect(start + paintedIcon.width() + 6, content.top(), std::max(0, total - paintedIcon.width() - 6), content.height()),
                Qt::AlignVCenter | Qt::AlignLeft | Qt::TextHideMnemonic, text);
        } else if (showText) painter->drawText(content, Qt::AlignCenter | Qt::TextHideMnemonic, text);
        painter->restore();
    }
};

QPalette themePalette(DesignerTheme theme)
{
    const auto color = [theme](DesignerColor role) { return colorFor(role, theme); };
    QPalette palette;
    palette.setColor(QPalette::Window, color(DesignerColor::PanelAlternate));
    palette.setColor(QPalette::WindowText, color(DesignerColor::Ink));
    palette.setColor(QPalette::Base, color(DesignerColor::Panel));
    palette.setColor(QPalette::AlternateBase, color(DesignerColor::Canvas));
    palette.setColor(QPalette::Text, color(DesignerColor::Ink));
    palette.setColor(QPalette::PlaceholderText, color(DesignerColor::Muted));
    palette.setColor(QPalette::Button, color(DesignerColor::Control));
    palette.setColor(QPalette::ButtonText, color(DesignerColor::Ink));
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, color(DesignerColor::Tint));
    palette.setColor(QPalette::HighlightedText, color(DesignerColor::Accent));
    palette.setColor(QPalette::Accent, color(DesignerColor::Accent));
    palette.setColor(QPalette::Light, color(DesignerColor::Panel));
    palette.setColor(QPalette::Midlight, color(DesignerColor::PanelAlternate));
    palette.setColor(QPalette::Mid, color(DesignerColor::Border));
    palette.setColor(QPalette::Dark, color(DesignerColor::Canvas));
    palette.setColor(QPalette::Shadow, color(DesignerColor::Shadow));
    palette.setColor(QPalette::Link, color(DesignerColor::Accent));
    palette.setColor(QPalette::LinkVisited, color(DesignerColor::Accent));
    palette.setColor(QPalette::ToolTipBase, color(DesignerColor::Panel));
    palette.setColor(QPalette::ToolTipText, color(DesignerColor::Ink));
    QColor disabled = color(DesignerColor::Ink); disabled.setAlphaF(0.48f);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText})
        palette.setColor(QPalette::Disabled, role, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, color(DesignerColor::PanelAlternate));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, color(DesignerColor::Muted));
    return palette;
}
} // namespace

QColor designerColor(DesignerColor color) { return colorFor(color, currentTheme); }

void installDesignerStyle()
{
    if (installedStyle) return;
    installedStyle = new DesignerStyle;
    QApplication::setStyle(installedStyle.data());
    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(12);
    QApplication::setFont(font);
    QApplication::setPalette(themePalette(currentTheme));
}

void setDesignerTheme(DesignerTheme theme)
{
    installDesignerStyle();
    if (currentTheme == theme) return;
    currentTheme = theme;
    QApplication::setPalette(themePalette(theme));
}

DesignerTheme designerTheme() { return currentTheme; }

} // namespace Designer
