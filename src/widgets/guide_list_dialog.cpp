#include "guide_list_dialog.hpp"

#include "designer_icons.hpp"
#include "designer_theme.hpp"
#include "dot_spin_box.hpp"
#include "workspace_widgets.hpp"

#include <QDialogButtonBox>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>

namespace Designer {
using namespace Qt::StringLiterals;

namespace {
constexpr int maximumGuides = 128;
constexpr int horizontalRole = Qt::UserRole;
constexpr int coordinateRole = Qt::UserRole + 1;
enum Column { AxisColumn, PositionColumn, DotsColumn, DeleteColumn };

CanvasGuide guideAt(const QModelIndex& index)
{
    return {index.siblingAtColumn(AxisColumn).data(horizontalRole).toBool(),
            index.siblingAtColumn(AxisColumn).data(coordinateRole).toInt()};
}
} // namespace

// Paints each cell as the concept table does and edits positions in dots.
class GuideDelegate final : public QStyledItemDelegate {
public:
    explicit GuideDelegate(GuideListDialog* dialog) : QStyledItemDelegate(dialog), dialog_(dialog) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const CanvasGuide guide = guideAt(index);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->save();
        painter->fillRect(option.rect, designerColor(selected ? DesignerColor::Tint : DesignerColor::Panel));
        QColor line = designerColor(DesignerColor::Border);
        line.setAlphaF(0.55f);
        if (index.row() > 0) painter->fillRect(QRect(option.rect.topLeft(), QSize(option.rect.width(), 1)), line);
        QRect content = option.rect;
        if (index.column() == AxisColumn) content.adjust(10, 0, 0, 0);
        QFont font = option.font;
        switch (index.column()) {
        case AxisColumn: {
            // Guide icons keep the paper ink in the light theme, as on the canvas.
            const QColor ink = designerTheme() == DesignerTheme::Light ? QColor(0x24, 0x6b, 0xdd)
                                                                       : designerColor(DesignerColor::Accent);
            const QRect icon(content.left(), content.center().y() - 7, 14, 14);
            paintDesignerIcon(painter, icon, guide.horizontal ? DesignerIcon::GuideHorizontal
                                                               : DesignerIcon::GuideVertical, ink);
            font.setWeight(QFont::DemiBold);
            painter->setFont(font);
            painter->setPen(designerColor(DesignerColor::Ink));
            painter->drawText(content.adjusted(21, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                              guide.horizontal ? u"Y"_s : u"X"_s);
            break;
        }
        case PositionColumn: {
            painter->setPen(designerColor(DesignerColor::Ink));
            const QString text = formatCanvasLength(guide.coordinate, dialog_->settings_.unit, dialog_->dpi_, option.locale);
            painter->drawText(content, Qt::AlignVCenter | Qt::AlignLeft, text);
            if (std::abs(2 * guide.coordinate - dialog_->extent(guide.horizontal)) <= 1) {
                // Lines through the label centre carry a "центр" tag.
                QFont tagFont = font;
                setFontPixels(tagFont, 10.5);
                const QFontMetricsF tagMetrics(tagFont);
                const QString tag = GuideListDialog::tr("центр");
                const QRectF box(content.left() + QFontMetricsF(font).horizontalAdvance(text) + 8,
                                 content.center().y() - 8, tagMetrics.horizontalAdvance(tag) + 12, 16);
                painter->setRenderHint(QPainter::Antialiasing);
                painter->setPen(line);
                painter->setBrush(designerColor(DesignerColor::PanelAlternate));
                painter->drawRoundedRect(box.adjusted(.5, .5, -.5, -.5), 3, 3);
                painter->setFont(tagFont);
                painter->setPen(designerColor(DesignerColor::Muted));
                painter->drawText(box, Qt::AlignCenter, tag);
            }
            break;
        }
        case DotsColumn: {
            QFont numbers = font;
            numbers.setPixelSize(11);
            painter->setFont(numbers);
            painter->setPen(designerColor(selected ? DesignerColor::Ink : DesignerColor::Muted));
            painter->drawText(content.adjusted(0, 0, -6, 0), Qt::AlignVCenter | Qt::AlignRight,
                              option.locale.toString(guide.coordinate));
            break;
        }
        case DeleteColumn:
            paintDesignerIcon(painter, QRectF(content.center().x() - 6.5, content.center().y() - 6.5, 14, 14),
                              DesignerIcon::Close,
                              designerColor(selected ? DesignerColor::Danger : DesignerColor::Muted));
            break;
        }
        painter->restore();
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex& index) const override
    {
        if (index.column() != PositionColumn) return nullptr;
        auto* editor = new DotSpinBox(parent);
        editor->setObjectName(u"guidePositionEditor"_s);
        editor->setMillimeterDecimals(2);
        editor->setDisplayUnits(dialog_->settings_.unit, dialog_->dpi_);
        editor->setRange(0, dialog_->extent(guideAt(index).horizontal));
        editor->setMinimumHeight(0);
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        static_cast<DotSpinBox*>(editor)->setValue(guideAt(index).coordinate);
    }

    void setModelData(QWidget* editor, QAbstractItemModel*, const QModelIndex& index) const override
    {
        auto* spin = static_cast<DotSpinBox*>(editor);
        spin->interpretText();
        const CanvasGuide guide = guideAt(index);
        const int value = spin->value();
        // Rebuilding rows inside the view's commit would delete its editor.
        QMetaObject::invokeMethod(dialog_, [dialog = dialog_, guide, value] { dialog->moveGuide(guide, value); },
                                  Qt::QueuedConnection);
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        editor->setGeometry(option.rect.left(), option.rect.center().y() - 12, std::min(116, option.rect.width()), 24);
    }

private:
    GuideListDialog* dialog_;
};

GuideListDialog::GuideListDialog(CanvasLayoutSettings settings, QSize page, int dpi, QWidget* parent)
    : QDialog(parent), settings_(std::move(settings)), page_(page), dpi_(std::max(1, dpi))
{
    setObjectName(u"guideListDialog"_s);
    setWindowTitle(tr("Направляющие"));
    resize(468, 420);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 14, 18, 14);
    layout->setSpacing(10);

    auto* head = new QHBoxLayout;
    head->setSpacing(8);
    head->addWidget(heading(tr("Направляющие"), this));
    count_ = new PillBadge(QString(), this);
    count_->setObjectName(u"guideListCount"_s);
    count_->setToolTip(tr("До 128 горизонтальных и 128 вертикальных направляющих"));
    head->addWidget(count_);
    head->addStretch();
    const auto add = [this, head](const QString& text, const QString& name) {
        auto* button = new QPushButton(designerIcon(DesignerIcon::Plus), text, this);
        button->setObjectName(name);
        button->setProperty("compact", true);
        button->setIconSize({14, 14});
        button->setFixedHeight(24);
        head->addWidget(button);
        return button;
    };
    addHorizontal_ = add(tr("Горизонтальная"), u"guideListAddHorizontal"_s);
    addVertical_ = add(tr("Вертикальная"), u"guideListAddVertical"_s);
    layout->addLayout(head);

    table_ = new QTableWidget(0, 4, this);
    table_->setObjectName(u"guideListTable"_s);
    table_->setHorizontalHeaderLabels({tr("Ось"), tr("Положение"), u"dots"_s, QString()});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeaderItem(DotsColumn)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(31);
    table_->horizontalHeader()->setFixedHeight(26);
    table_->horizontalHeader()->setSectionResizeMode(PositionColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(AxisColumn, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(DotsColumn, QHeaderView::Fixed);
    table_->horizontalHeader()->setSectionResizeMode(DeleteColumn, QHeaderView::Fixed);
    table_->horizontalHeader()->setHighlightSections(false);
    table_->setColumnWidth(AxisColumn, 66);
    table_->setColumnWidth(DotsColumn, 60);
    table_->setColumnWidth(DeleteColumn, 34);
    table_->setShowGrid(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                            | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    table_->setItemDelegate(new GuideDelegate(this));
    table_->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(table_, 1);

    auto* tail = new QHBoxLayout;
    auto* hint = caption(tr("На холсте: тяните из линейки — добавить, за этикетку — удалить."), this, 11);
    hint->setWordWrap(true);
    tail->addWidget(hint, 1);
    removeAll_ = new QPushButton(designerIcon(DesignerIcon::Trash, DesignerColor::Danger, DesignerColor::Danger),
                                 tr("Удалить все"), this);
    removeAll_->setObjectName(u"guideListRemoveAll"_s);
    removeAll_->setProperty("flat", true);
    removeAll_->setProperty("compact", true);
    removeAll_->setProperty("tone", u"danger"_s);
    removeAll_->setIconSize({14, 14});
    removeAll_->setFixedHeight(24);
    tail->addWidget(removeAll_);
    layout->addLayout(tail);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Применить"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(addHorizontal_, &QPushButton::clicked, this, [this] { addGuide(true); });
    connect(addVertical_, &QPushButton::clicked, this, [this] { addGuide(false); });
    connect(removeAll_, &QPushButton::clicked, this, [this] {
        settings_.horizontalGuides.clear();
        settings_.verticalGuides.clear();
        refresh();
    });
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column == DeleteColumn) removeGuide(guideAt(table_->model()->index(row, AxisColumn)));
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { emit currentGuideChanged(currentGuide()); });
    refresh();
}

QList<int>& GuideListDialog::guides(bool horizontal)
{
    return horizontal ? settings_.horizontalGuides : settings_.verticalGuides;
}

int GuideListDialog::extent(bool horizontal) const
{
    return horizontal ? page_.height() : page_.width();
}

bool GuideListDialog::addGuide(bool horizontal)
{
    auto& values = guides(horizontal);
    if (values.size() >= maximumGuides) return false;
    const int size = extent(horizontal);
    const int step = std::max(1, settings_.gridStep);
    // The centre first, then alternately one grid step after and before it.
    for (int offset = 0; offset <= size; offset += step) {
        for (const int candidate : {size / 2 + offset, size / 2 - offset}) {
            if (candidate < 0 || candidate > size || values.contains(candidate)) continue;
            values.append(candidate);
            std::ranges::sort(values);
            refresh(CanvasGuide{horizontal, candidate});
            return true;
        }
    }
    return false;
}

void GuideListDialog::moveGuide(CanvasGuide guide, int coordinate)
{
    auto& values = guides(guide.horizontal);
    if (!values.contains(guide.coordinate)) return;
    coordinate = std::clamp(coordinate, 0, extent(guide.horizontal));
    values.removeOne(guide.coordinate);
    if (!values.contains(coordinate)) values.append(coordinate);
    std::ranges::sort(values);
    refresh(CanvasGuide{guide.horizontal, coordinate});
}

void GuideListDialog::removeGuide(CanvasGuide guide)
{
    guides(guide.horizontal).removeOne(guide.coordinate);
    refresh();
}

std::optional<CanvasGuide> GuideListDialog::currentGuide() const
{
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return std::nullopt;
    return guideAt(rows.front());
}

void GuideListDialog::refresh(std::optional<CanvasGuide> select)
{
    const QSignalBlocker blocker(table_);
    table_->clearContents();
    table_->setRowCount(0);
    int selectedRow = -1;
    for (const bool horizontal : {true, false}) {
        for (const int coordinate : guides(horizontal)) {
            const int row = table_->rowCount();
            table_->insertRow(row);
            for (int column = 0; column < table_->columnCount(); ++column) {
                auto* item = new QTableWidgetItem;
                item->setData(horizontalRole, horizontal);
                item->setData(coordinateRole, coordinate);
                // Accessible text for screen readers and tests.
                item->setText(column == AxisColumn ? (horizontal ? u"Y"_s : u"X"_s)
                              : column == PositionColumn ? formatCanvasLength(coordinate, settings_.unit, dpi_, locale())
                              : column == DotsColumn ? QString::number(coordinate) : QString());
                if (column != PositionColumn) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                table_->setItem(row, column, item);
            }
            if (select == CanvasGuide{horizontal, coordinate}) selectedRow = row;
        }
    }
    if (selectedRow >= 0) table_->selectRow(selectedRow);
    const auto total = settings_.horizontalGuides.size() + settings_.verticalGuides.size();
    count_->setText(tr("%1 из %2").arg(total).arg(2 * maximumGuides));
    addHorizontal_->setEnabled(settings_.horizontalGuides.size() < maximumGuides);
    addVertical_->setEnabled(settings_.verticalGuides.size() < maximumGuides);
    removeAll_->setEnabled(total > 0);
    emit currentGuideChanged(currentGuide());
}

} // namespace Designer
