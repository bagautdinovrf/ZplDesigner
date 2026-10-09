#include "layers_list.hpp"

#include "designer_theme.hpp"
#include "element_presentation.hpp"

#include <QPainter>
#include <QSignalBlocker>
#include <QStyledItemDelegate>

#include <algorithm>

namespace Designer {
namespace {
constexpr int visibilityRole = Qt::UserRole + 1;
constexpr int identityRole = Qt::UserRole + 2;

class LayerDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {170, 37}; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect rect = option.rect.adjusted(0, 1, 0, -1);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        if (selected || option.state.testFlag(QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(designerColor(DesignerColor::Tint));
            painter->drawRoundedRect(rect, 4, 4);
        }
        const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        icon.paint(painter, QRect(rect.left() + 7, rect.center().y() - 8, 16, 16));
        auto font = option.font;
        font.setPixelSize(11);
        painter->setFont(font);
        painter->setPen(designerColor(selected ? DesignerColor::Accent : DesignerColor::Ink));
        const QRect nameRect = rect.adjusted(33, 0, -33, 0);
        painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(font).elidedText(index.data().toString(), Qt::ElideRight, nameRect.width()));
        font.setPixelSize(9);
        painter->setFont(font);
        painter->setPen(designerColor(DesignerColor::Muted));
        painter->drawText(rect.adjusted(0, 0, -5, 0), Qt::AlignRight | Qt::AlignVCenter,
                          index.data(visibilityRole).toString());
        painter->restore();
    }
};
} // namespace

LayersList::LayersList(QWidget* parent) : QListWidget(parent)
{
    setObjectName(QStringLiteral("layerList"));
    setFrameShape(QFrame::NoFrame);
    setItemDelegate(new LayerDelegate(this));
    setMouseTracking(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
}

void LayersList::refresh(const DesignDocument& document, const QList<QUuid>& selectedIds)
{
    const QSignalBlocker blocker(this);
    clear();
    for (qsizetype i = document.elements.size(); i > 0; --i) {
        const auto& element = document.elements[i - 1];
        const QString title = (element.locked ? QStringLiteral("🔒 ") : QString{})
            + (!element.editorVisible ? QStringLiteral("◌ ") : QString{})
            + (!element.printable ? QStringLiteral("⊘ ") : QString{})
            + (element.groupId.isNull() ? QString{} : QStringLiteral("▣ "))
            + (element.name.isEmpty() ? elementTypeName(element.type) : element.name);
        auto* item = new QListWidgetItem(elementIcon(element.type), title, this);
        item->setToolTip(QStringLiteral("%1\nНа холсте: %2 · Печать: %3 · %4")
            .arg(elementTypeName(element.type), element.editorVisible ? QStringLiteral("да") : QStringLiteral("нет"),
                 element.printable ? QStringLiteral("да") : QStringLiteral("нет"),
                 element.locked ? QStringLiteral("заблокирован") : QStringLiteral("доступен для правок")));
        item->setData(Qt::UserRole, static_cast<int>(i - 1));
        item->setData(visibilityRole, element.editorVisible ? QStringLiteral("виден") : QStringLiteral("скрыт"));
        item->setData(identityRole, element.id);
    }
    setMaximumHeight(std::min(370, static_cast<int>(document.elements.size()) * 37 + 2));
    setSelectedIds(selectedIds);
}

void LayersList::setSelectedIds(const QList<QUuid>& ids)
{
    const QSignalBlocker blocker(this);
    clearSelection();
    QListWidgetItem* primary = nullptr;
    qsizetype primaryRank = -1;
    for (int row = 0; row < count(); ++row) {
        auto* entry = item(row);
        const qsizetype rank = ids.indexOf(entry->data(identityRole).toUuid());
        entry->setSelected(rank >= 0);
        if (rank > primaryRank) {
            primary = entry;
            primaryRank = rank;
        }
    }
    setCurrentItem(primary, QItemSelectionModel::NoUpdate);
}

QList<QUuid> LayersList::selectedIds() const
{
    QList<QUuid> ids;
    for (const auto* item : selectedItems()) ids.append(item->data(identityRole).toUuid());
    const auto primary = primaryId();
    if (ids.removeOne(primary)) ids.append(primary);
    return ids;
}

QUuid LayersList::primaryId() const
{
    return currentItem() && currentItem()->isSelected() ? currentItem()->data(identityRole).toUuid() : QUuid{};
}

} // namespace Designer
