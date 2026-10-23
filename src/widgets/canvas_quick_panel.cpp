#include "canvas_quick_panel.hpp"

#include "designer_icons.hpp"
#include "document_session.hpp"
#include "dot_spin_box.hpp"
#include "workspace_widgets.hpp"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>
#include <vector>

namespace Designer {
using namespace Qt::StringLiterals;

namespace {
constexpr int cardWidth = 310;
// Room for the arrow above the card and for its two-layer shadow.
constexpr QMargins shadowMargins(40, 12, 40, 64);
// The arrow is a 10 px square turned 45 degrees, centred 22 px from the right
// edge on the top border; the card starts 13 px below the anchor and ends 2 px
// to the right of it, exactly as positioned in the concept.
constexpr int arrowInset = 22;
constexpr int anchorGap = 13;
constexpr int anchorOverhang = 2;

// Unit choice frame: a 1 px border around 2 px of padding.
class SegmentFrame final : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(designerColor(DesignerColor::Border));
        p.setBrush(designerColor(DesignerColor::Control));
        p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 5, 5);
    }
};

// "[Alt] — временно выкл.": a key cap with a thicker bottom edge and a hint.
class KeyHint final : public QWidget {
public:
    KeyHint(QString key, QString text, QWidget* parent) : QWidget(parent), key_(std::move(key)), text_(std::move(text))
    {
        setFontPixels(keyFont_, 10.5);
        textFont_.setPixelSize(11);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override
    {
        return {qCeil(keyWidth() + 6 + QFontMetricsF(textFont_).horizontalAdvance(text_)), 18};
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF cap(0, (height() - 18) / 2.0, keyWidth(), 18);
        p.setPen(Qt::NoPen);
        p.setBrush(designerColor(DesignerColor::Border));
        p.drawRoundedRect(cap, 3, 3);
        p.setBrush(designerColor(DesignerColor::Control));
        p.drawRoundedRect(cap.adjusted(1, 1, -1, -2), 2, 2);
        p.setFont(keyFont_);
        p.setPen(designerColor(DesignerColor::Ink));
        p.drawText(cap.adjusted(1, 1, -1, -2), Qt::AlignCenter, key_);
        p.setFont(textFont_);
        p.setPen(designerColor(DesignerColor::Muted));
        p.drawText(QRectF(cap.right() + 6, 0, width() - cap.right() - 6, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text_);
    }
private:
    qreal keyWidth() const { return QFontMetricsF(keyFont_).horizontalAdvance(key_) + 10; }
    QString key_;
    QString text_;
    QFont keyFont_ = font();
    QFont textFont_ = font();
};

// Three box blurs approximate a Gaussian, as the W3C filter effects do.
void blur(std::vector<float>& data, int width, int height, qreal sigma)
{
    int size = std::max(1, static_cast<int>(std::floor(sigma * 3 * std::sqrt(2 * std::numbers::pi) / 4 + 0.5)));
    if (size % 2 == 0) ++size;
    const int radius = size / 2;
    std::vector<float> line(static_cast<std::size_t>(std::max(width, height)));
    const auto pass = [&](int count, int length, auto at) {
        for (int index = 0; index < count; ++index) {
            float sum = 0;
            for (int i = 0; i < std::min(radius, length); ++i) sum += at(index, i);
            for (int i = 0; i < length; ++i) {
                if (i + radius < length) sum += at(index, i + radius);
                line[static_cast<std::size_t>(i)] = sum / size;
                if (i - radius >= 0) sum -= at(index, i - radius);
            }
            for (int i = 0; i < length; ++i) at(index, i) = line[static_cast<std::size_t>(i)];
        }
    };
    for (int repeat = 0; repeat < 3; ++repeat) {
        pass(height, width, [&](int y, int x) -> float& { return data[static_cast<std::size_t>(y) * width + x]; });
        pass(width, height, [&](int x, int y) -> float& { return data[static_cast<std::size_t>(y) * width + x]; });
    }
}
} // namespace

CanvasQuickPanel::CanvasQuickPanel(DocumentSession* session, QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint), session_(session)
{
    setObjectName(u"canvasQuickPanel"_s);
    setAttribute(Qt::WA_TranslucentBackground);
    setAccessibleName(tr("Холст и направляющие"));
    auto* layout = new QVBoxLayout(this);
    // Content starts inside the card's one-pixel border.
    layout->setContentsMargins(shadowMargins + QMargins(1, 1, 1, 1));
    layout->setSpacing(0);

    auto* head = new QHBoxLayout;
    head->setContentsMargins(12, 12, 12, 8);
    head->setSpacing(8);
    auto* title = new QLabel(tr("Холст и направляющие"), this);
    QFont titleFont = title->font();
    titleFont.setPixelSize(13);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    head->addWidget(title);
    head->addStretch();
    // Two exclusive options in a framed segment, as the concept's "мм | dots".
    const auto segment = [this](const QString& first, const QString& firstName,
                                const QString& second, const QString& secondName) {
        auto* frame = new SegmentFrame(this);
        auto* options = new QHBoxLayout(frame);
        options->setContentsMargins(3, 3, 3, 3);
        options->setSpacing(2);
        auto* group = new QButtonGroup(frame);
        const auto choice = [&](const QString& text, const QString& name) {
            auto* button = new QPushButton(text, frame);
            button->setObjectName(name);
            button->setCheckable(true);
            button->setProperty("designerControl", u"choice"_s);
            QFont bold = button->font();
            bold.setWeight(QFont::DemiBold);
            button->setFixedSize(qCeil(QFontMetricsF(bold).horizontalAdvance(text)) + 22, 22);
            group->addButton(button);
            options->addWidget(button);
            return button;
        };
        return std::tuple{frame, choice(first, firstName), choice(second, secondName)};
    };
    auto [units, millimeters, dots] = segment(tr("мм"), u"quickPanelMillimeters"_s, u"dots"_s, u"quickPanelDots"_s);
    millimeters_ = millimeters;
    dots_ = dots;
    units->setToolTip(tr("Единицы линеек и полей панели. Положения всегда хранятся в целых точках принтера."));
    head->addWidget(units);
    layout->addLayout(head);

    const auto row = [&](int spacing = 8) {
        auto* widget = new QWidget(this);
        widget->setMinimumHeight(34);
        auto* line = new QHBoxLayout(widget);
        line->setContentsMargins(12, 0, 12, 0);
        line->setSpacing(spacing);
        layout->addWidget(widget);
        return line;
    };
    const auto check = [this](const QString& text, const QString& name, const QString& tip) {
        auto* box = new QCheckBox(text, this);
        box->setObjectName(name);
        box->setToolTip(tip);
        return box;
    };
    const auto aside = [](QHBoxLayout* line) {
        auto* group = new QHBoxLayout;
        group->setSpacing(6);
        line->addStretch();
        line->addLayout(group);
        return group;
    };
    const auto spin = [this](const QString& name, const QString& tip) {
        auto* box = new DotSpinBox(this);
        box->setObjectName(name);
        box->setMillimeterDecimals(2);
        box->setFixedSize(98, 24);
        box->setToolTip(tip);
        return box;
    };

    auto* line = row();
    grid_ = check(tr("Сетка"), u"quickPanelGrid"_s, tr("Сетка на этикетке. Скрытая сетка продолжает задавать шаг привязки."));
    line->addWidget(grid_);
    auto* side = aside(line);
    auto* step = caption(tr("шаг"), this, 11);
    side->addWidget(step);
    gridStep_ = spin(u"quickPanelGridStep"_s, tr("Шаг сетки и привязки. Хранится в целых точках принтера."));
    gridStep_->setRange(1, 4096);
    side->addWidget(gridStep_);

    line = row();
    snap_ = check(tr("Привязка"), u"quickPanelSnap"_s,
                  tr("Привязка к сетке, направляющим, рамке отступа и соседним объектам."));
    line->addWidget(snap_);
    aside(line)->addWidget(new KeyHint(u"Alt"_s, tr("— временно выкл."), this));

    line = row();
    margins_ = check(tr("Рамка отступа"), u"quickPanelMargins"_s,
                     tr("Рамка безопасного отступа от края этикетки. Не печатается и не сдвигает содержимое."));
    line->addWidget(margins_);
    margin_ = spin(u"quickPanelMargin"_s, tr("Отступ от края этикетки, меньше половины её ширины и высоты."));
    margin_->setRange(0, 2048);
    aside(line)->addWidget(margin_);

    line = row();
    rulers_ = check(tr("Линейки"), u"quickPanelRulers"_s,
                    tr("Линейки у этикетки. Потяните из линейки, чтобы добавить направляющую."));
    line->addWidget(rulers_);
    auto [placement, atLabel, docked] = segment(tr("у этикетки"), u"quickPanelRulersAtLabel"_s,
                                                tr("у края"), u"quickPanelRulersDocked"_s);
    rulersAtLabel_ = atLabel;
    rulersDocked_ = docked;
    rulersAtLabel_->setChecked(true);
    placement->setToolTip(tr("Линейки рядом с этикеткой или у верхнего и левого края холста. "
                             "Общая настройка редактора для всех этикеток; не отменяется через Ctrl+Z."));
    aside(line)->addWidget(placement);

    line = row();
    guides_ = check(tr("Направляющие"), u"quickPanelGuides"_s,
                    tr("Направляющие видны только в редакторе: в ZPL, превью и PNG их нет."));
    line->addWidget(guides_);
    side = aside(line);
    guideCount_ = new PillBadge(u"0"_s, this);
    guideCount_->setObjectName(u"quickPanelGuideCount"_s);
    guideCount_->setToolTip(tr("Число направляющих"));
    side->addWidget(guideCount_);
    auto* list = new QPushButton(tr("Список…"), this);
    list->setObjectName(u"quickPanelGuideList"_s);
    list->setProperty("compact", true);
    list->setToolTip(tr("Таблица направляющих: точные положения, добавление и удаление"));
    QFont small = list->font();
    small.setPixelSize(11);
    list->setFixedSize(qCeil(QFontMetricsF(small).horizontalAdvance(list->text())) + 20, 24);
    side->addWidget(list);

    layout->addSpacing(6);
    layout->addWidget(new PanelSeparator(this));
    auto* sub = new QHBoxLayout;
    sub->setContentsMargins(12, 10, 12, 0);
    sub->setSpacing(6);
    sub->addWidget(caption(tr("Подписи выделения"), this, 11));
    auto* scope = new PillBadge(tr("все этикетки"), this);
    scope->setToolTip(tr("Общая настройка редактора: действует для всех этикеток, не сохраняется в файл и не отменяется через Ctrl+Z."));
    sub->addWidget(scope);
    sub->addStretch();
    layout->addLayout(sub);
    line = row(22);
    geometrySizes_ = check(tr("Размеры"), u"quickPanelGeometrySizes"_s, tr("Размеры выделенного объекта на холсте"));
    geometryCoordinates_ = check(tr("Координаты"), u"quickPanelGeometryCoordinates"_s,
                                 tr("Координаты ^FO/^FT выделенного объекта на холсте"));
    geometrySizes_->setChecked(true);
    geometryCoordinates_->setChecked(true);
    line->addWidget(geometrySizes_);
    line->addWidget(geometryCoordinates_);
    line->addStretch();

    layout->addSpacing(6);
    layout->addWidget(new PanelSeparator(this));
    auto* actions = new QHBoxLayout;
    // In the concept the wide delete command reaches into the right padding
    // and ends at the border; both commands keep their natural widths.
    actions->setContentsMargins(12, 10, 0, 10);
    actions->setSpacing(6);
    const auto flat = [&](const QString& text, const QString& name, DesignerIcon icon, DesignerColor tone,
                          const QString& toneName, const QString& tip) {
        auto* button = new QPushButton(designerIcon(icon, tone, tone), text, this);
        button->setObjectName(name);
        button->setProperty("flat", true);
        button->setProperty("compact", true);
        button->setProperty("tone", toneName);
        button->setIconSize({14, 14});
        button->setToolTip(tip);
        button->setFixedSize(qCeil(QFontMetricsF(small).horizontalAdvance(text)) + 14 + 6 + 18, 24);
        actions->addWidget(button);
        return button;
    };
    auto* reset = flat(tr("По умолчанию"), u"quickPanelReset"_s, DesignerIcon::Reset, DesignerColor::Muted,
                       u"muted"_s, tr("Вернуть сетку, привязку, отступ, линейки, единицы и подписи выделения. Направляющие остаются."));
    actions->addStretch();
    removeGuides_ = flat(tr("Удалить направляющие"), u"quickPanelRemoveGuides"_s, DesignerIcon::Trash,
                         DesignerColor::Danger, u"danger"_s, tr("Удалить все направляющие этой этикетки"));
    layout->addLayout(actions);

    footer_ = new QWidget(this);
    auto* footerLayout = new QVBoxLayout(footer_);
    footerLayout->setContentsMargins(12, 10, 12, 9);
    auto* note = caption(tr("Применяется сразу. Ctrl+Z отменяет всё, что изменено в панели, одним шагом."), footer_, 11);
    note->setObjectName(u"quickPanelNote"_s);
    note->setWordWrap(true);
    footerLayout->addWidget(note);
    layout->addWidget(footer_);

    connect(millimeters_, &QAbstractButton::clicked, this, [this] {
        apply([](CanvasLayoutSettings& settings) { settings.unit = CanvasUnit::Millimeters; });
    });
    connect(dots_, &QAbstractButton::clicked, this, [this] {
        apply([](CanvasLayoutSettings& settings) { settings.unit = CanvasUnit::Dots; });
    });
    const auto toggle = [this](QCheckBox* box, bool CanvasLayoutSettings::* member) {
        connect(box, &QCheckBox::toggled, this, [this, member](bool on) {
            apply([member, on](CanvasLayoutSettings& settings) { settings.*member = on; });
        });
    };
    toggle(grid_, &CanvasLayoutSettings::gridVisible);
    toggle(snap_, &CanvasLayoutSettings::snapEnabled);
    toggle(rulers_, &CanvasLayoutSettings::rulersVisible);
    toggle(guides_, &CanvasLayoutSettings::guidesVisible);
    connect(margins_, &QCheckBox::toggled, this, [this](bool on) {
        const int limit = margin_->maximum();
        apply([on, limit](CanvasLayoutSettings& settings) {
            settings.marginsVisible = on;
            // A frame wider than half the label would not be drawn at all.
            if (on) settings.margin = std::min(settings.margin, limit);
        });
    });
    connect(gridStep_, &QSpinBox::valueChanged, this, [this](int value) {
        apply([value](CanvasLayoutSettings& settings) { settings.gridStep = value; });
    });
    connect(margin_, &QSpinBox::valueChanged, this, [this](int value) {
        apply([value](CanvasLayoutSettings& settings) { settings.margin = value; });
    });
    connect(list, &QPushButton::clicked, this, [this] {
        close();
        emit guideListRequested();
    });
    connect(removeGuides_, &QPushButton::clicked, this, [this] {
        apply([](CanvasLayoutSettings& settings) {
            settings.horizontalGuides.clear();
            settings.verticalGuides.clear();
        });
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        apply([](CanvasLayoutSettings& settings) {
            CanvasLayoutSettings defaults;
            defaults.horizontalGuides = std::move(settings.horizontalGuides);
            defaults.verticalGuides = std::move(settings.verticalGuides);
            settings = std::move(defaults);
        });
        setGeometryLabelsVisible(true, true);
        setRulersDocked(false);
    });
    for (auto* button : {rulersAtLabel_, rulersDocked_})
        connect(button, &QAbstractButton::clicked, this, [this] { emit rulersDockedChanged(rulersDocked()); });
    for (auto* box : {geometrySizes_, geometryCoordinates_}) {
        connect(box, &QCheckBox::toggled, this, [this] {
            emit geometryLabelsChanged(geometrySizes_->isChecked(), geometryCoordinates_->isChecked());
        });
    }
    connect(session_, &DocumentSession::layoutSettingsChanged, this, &CanvasQuickPanel::refresh);
    connect(session_, &DocumentSession::documentChanged, this, &CanvasQuickPanel::refresh);
    refresh();
    const int width = cardWidth + shadowMargins.left() + shadowMargins.right();
    setFixedWidth(width);
    resize(width, layout->hasHeightForWidth() ? layout->totalHeightForWidth(width) : layout->totalSizeHint().height());
}

void CanvasQuickPanel::setGeometryLabelsVisible(bool sizes, bool coordinates)
{
    // Setting both before notifying reports one consistent pair.
    {
        const QSignalBlocker sizesBlocker(geometrySizes_), coordinatesBlocker(geometryCoordinates_);
        if (geometrySizes_->isChecked() == sizes && geometryCoordinates_->isChecked() == coordinates) return;
        geometrySizes_->setChecked(sizes);
        geometryCoordinates_->setChecked(coordinates);
    }
    if (isVisible()) emit geometryLabelsChanged(sizes, coordinates);
}

void CanvasQuickPanel::setRulersDocked(bool docked)
{
    if (rulersDocked() == docked) return;
    (docked ? rulersDocked_ : rulersAtLabel_)->setChecked(true);
    if (isVisible()) emit rulersDockedChanged(docked);
}

bool CanvasQuickPanel::rulersDocked() const { return rulersDocked_->isChecked(); }

bool CanvasQuickPanel::geometrySizesVisible() const { return geometrySizes_->isChecked(); }

bool CanvasQuickPanel::geometryCoordinatesVisible() const { return geometryCoordinates_->isChecked(); }

void CanvasQuickPanel::popup(QWidget* anchor)
{
    anchor_ = anchor;
    group_ = session_->newLayoutEditGroup();
    setAttribute(Qt::WA_NoMouseReplay, false);
    refresh();
    const int width = cardWidth + shadowMargins.left() + shadowMargins.right();
    resize(width, layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width)
                                                : layout()->totalSizeHint().height());
    const QPoint corner = anchor->mapToGlobal(QPoint(anchor->width() + anchorOverhang, anchor->height() + anchorGap));
    QPoint origin(corner.x() - cardWidth - shadowMargins.left(), corner.y() - shadowMargins.top());
    if (const auto* screen = anchor->screen()) {
        // Keep the card itself on screen; the shadow may fall outside it.
        const QRect available = screen->availableGeometry();
        origin.setX(std::clamp(origin.x(), available.left() - shadowMargins.left(),
                               std::max(available.left(), available.right() + 1 - cardWidth) - shadowMargins.left()));
    }
    move(origin);
    show();
    activateWindow();
}

QRect CanvasQuickPanel::cardRect() const
{
    return QRect(shadowMargins.left(), shadowMargins.top(), cardWidth,
                 std::max(0, height() - shadowMargins.top() - shadowMargins.bottom()));
}

QPainterPath CanvasQuickPanel::cardPath() const
{
    const QRect card = cardRect();
    QPainterPath path;
    path.addRoundedRect(QRectF(card).adjusted(.5, .5, -.5, -.5), 7.5, 7.5);
    // Stroke centres of the turned 10 px square lie 0.5 px inside its edges.
    const qreal half = 4.5 * std::numbers::sqrt2;
    const QPointF center(card.left() + cardWidth - arrowInset, card.top());
    QPainterPath arrow;
    arrow.moveTo(center + QPointF(0, -half));
    arrow.lineTo(center + QPointF(half, 0));
    arrow.lineTo(center + QPointF(0, half));
    arrow.lineTo(center + QPointF(-half, 0));
    arrow.closeSubpath();
    return path.united(arrow);
}

const QImage& CanvasQuickPanel::shadow()
{
    if (shadowSize_ == size() && shadowTheme_ == designerTheme() && !shadow_.isNull()) return shadow_;
    shadowSize_ = size();
    shadowTheme_ = designerTheme();
    shadow_ = QImage(size(), QImage::Format_ARGB32_Premultiplied);
    shadow_.fill(Qt::transparent);
    struct Layer { int offset; qreal blur; QColor color; };
    // box-shadow: 0 22px 60px and 0 3px 10px of the concept's --dialog-shadow.
    const bool dark = shadowTheme_ == DesignerTheme::Dark;
    const Layer layers[] = {
        {22, 60, dark ? QColor(0, 0, 0, 0xaa) : QColor(0x1b, 0x2a, 0x40, 0x29)},
        {3, 10, dark ? QColor(0, 0, 0, 0x77) : QColor(0x1b, 0x2a, 0x40, 0x1c)}};
    const int w = width();
    const int h = height();
    QPainter result(&shadow_);
    for (const auto& layer : layers) {
        QImage mask(size(), QImage::Format_Alpha8);
        mask.fill(0);
        {
            QPainter shape(&mask);
            shape.setRenderHint(QPainter::Antialiasing);
            shape.setPen(Qt::NoPen);
            shape.setBrush(Qt::black);
            shape.drawRoundedRect(QRectF(cardRect().translated(0, layer.offset)), 8, 8);
        }
        std::vector<float> alpha(static_cast<std::size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            const uchar* source = mask.constScanLine(y);
            for (int x = 0; x < w; ++x) alpha[static_cast<std::size_t>(y) * w + x] = source[x] / 255.0f;
        }
        blur(alpha, w, h, layer.blur / 2);
        QImage colored(size(), QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < h; ++y) {
            auto* target = reinterpret_cast<QRgb*>(colored.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const float coverage = std::clamp(alpha[static_cast<std::size_t>(y) * w + x], 0.0f, 1.0f);
                const int a = qRound(coverage * layer.color.alpha());
                target[x] = qPremultiply(qRgba(layer.color.red(), layer.color.green(), layer.color.blue(), a));
            }
        }
        result.drawImage(0, 0, colored);
    }
    return shadow_;
}

void CanvasQuickPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.drawImage(0, 0, shadow());
    painter.setRenderHint(QPainter::Antialiasing);
    const QPainterPath card = cardPath();
    painter.fillPath(card, designerColor(DesignerColor::Panel));
    painter.save();
    painter.setClipPath(card);
    const QRect footer = footer_->geometry().adjusted(-1, 0, 1, 1);
    painter.fillRect(footer, designerColor(DesignerColor::PanelAlternate));
    painter.fillRect(QRect(footer.topLeft(), QSize(footer.width(), 1)), designerColor(DesignerColor::Border));
    painter.restore();
    painter.strokePath(card, QPen(designerColor(DesignerColor::Border), 1));
}

void CanvasQuickPanel::mousePressEvent(QMouseEvent* event)
{
    if (!cardPath().contains(event->position())) {
        // A press on the anchor closes the panel and must not reopen it.
        if (anchor_ && anchor_->rect().contains(anchor_->mapFromGlobal(event->globalPosition().toPoint())))
            setAttribute(Qt::WA_NoMouseReplay);
        close();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CanvasQuickPanel::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CanvasQuickPanel::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    emit closed();
}

void CanvasQuickPanel::refresh()
{
    const auto& settings = session_->layoutSettings();
    const auto& document = session_->document();
    const QSignalBlocker blockers[] = {
        QSignalBlocker(millimeters_), QSignalBlocker(dots_), QSignalBlocker(grid_), QSignalBlocker(snap_),
        QSignalBlocker(margins_), QSignalBlocker(rulers_), QSignalBlocker(guides_),
        QSignalBlocker(gridStep_), QSignalBlocker(margin_)};
    millimeters_->setChecked(settings.unit == CanvasUnit::Millimeters);
    dots_->setChecked(settings.unit == CanvasUnit::Dots);
    grid_->setChecked(settings.gridVisible);
    snap_->setChecked(settings.snapEnabled);
    margins_->setChecked(settings.marginsVisible);
    rulers_->setChecked(settings.rulersVisible);
    guides_->setChecked(settings.guidesVisible);
    for (auto* box : {gridStep_, margin_}) box->setDisplayUnits(settings.unit, document.dpi);
    gridStep_->setValue(settings.gridStep);
    margin_->setMaximum(std::clamp((std::min(document.width, document.height) - 1) / 2, 0, 2048));
    margin_->setValue(settings.margin);
    margin_->setEnabled(settings.marginsVisible);
    const auto count = settings.horizontalGuides.size() + settings.verticalGuides.size();
    guideCount_->setText(QString::number(count));
    removeGuides_->setEnabled(count > 0);
}

void CanvasQuickPanel::apply(const std::function<void(CanvasLayoutSettings&)>& change)
{
    if (!group_) group_ = session_->newLayoutEditGroup();
    auto settings = session_->layoutSettings();
    change(settings);
    session_->setLayoutSettings(std::move(settings), tr("Холст и направляющие"), group_);
}

} // namespace Designer
