#include "element_selection.hpp"

#include <QSet>

#include <utility>

namespace Designer {

ElementSelection::ElementSelection(QObject* parent)
    : QObject(parent)
{
}

const QList<QUuid>& ElementSelection::ids() const
{
    return ids_;
}

QUuid ElementSelection::primaryId() const
{
    return ids_.isEmpty() ? QUuid{} : ids_.back();
}

QList<int> ElementSelection::indices(const DesignDocument& document) const
{
    QList<int> result;
    if (!document.visualEditable) return result;
    for (const auto& id : ids_) {
        for (qsizetype index = 0; index < document.elements.size(); ++index) {
            if (document.elements[index].id == id) {
                result.append(static_cast<int>(index));
                break;
            }
        }
    }
    return result;
}

void ElementSelection::setIds(const QList<QUuid>& ids, const DesignDocument& document)
{
    QList<QUuid> selected;
    QSet<QUuid> selectedGroups;
    if (document.visualEditable) {
        for (const auto& id : ids) {
            if (id.isNull() || selected.contains(id)) continue;
            for (const auto& element : document.elements) {
                if (element.id != id) continue;
                selected.append(id);
                if (!element.groupId.isNull()) selectedGroups.insert(element.groupId);
                break;
            }
        }
        QList<QUuid> additional;
        for (const auto& element : document.elements) {
            if (selectedGroups.contains(element.groupId) && !element.id.isNull()
                && !selected.contains(element.id) && !additional.contains(element.id))
                additional.append(element.id);
        }
        selected = additional + selected;
    }
    if (selected == ids_) return;
    ids_ = std::move(selected);
    emit selectionChanged();
}

void ElementSelection::setIndices(const QList<int>& indices, const DesignDocument& document)
{
    QList<QUuid> ids;
    for (const int index : indices) {
        if (index >= 0 && index < document.elements.size()) ids.append(document.elements[index].id);
    }
    setIds(ids, document);
}

void ElementSelection::reconcile(const DesignDocument& document)
{
    setIds(ids_, document);
}

} // namespace Designer
