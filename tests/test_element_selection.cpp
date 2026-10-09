#include "element_selection.hpp"

#include <QSignalSpy>
#include <QtTest>

using namespace Designer;

namespace {
DesignDocument documentWithElements()
{
    DesignDocument document;
    for (int index = 0; index < 4; ++index)
        document.elements.append(makeElement(ElementType::Box, {20 + 30 * index, 20}));
    return document;
}
} // namespace

class ElementSelectionTests final : public QObject {
    Q_OBJECT
private slots:
    void reorderAndRestoredSnapshotKeepSelectedIdentities();
    void deletingSelectedObjectDoesNotSelectItsReplacementIndex();
    void groupExpansionKeepsRequestedPrimary();
    void invalidAndRepeatedSelectionsDoNotEmitChanges();
    void sourceOnlySnapshotClearsSelection();
};

void ElementSelectionTests::reorderAndRestoredSnapshotKeepSelectedIdentities()
{
    const auto original = documentWithElements();
    auto changed = original;
    ElementSelection selection;
    selection.setIndices({0, 2}, original);
    const auto selected = selection.ids();
    const auto primary = selection.primaryId();
    QSignalSpy notifications(&selection, &ElementSelection::selectionChanged);
    changed.elements.move(0, 3);
    selection.reconcile(changed);
    QCOMPARE(selection.ids(), selected);
    QCOMPARE(selection.indices(changed), (QList<int>{3, 1}));
    QCOMPARE(selection.primaryId(), primary);
    selection.reconcile(original);
    QCOMPARE(selection.ids(), selected);
    QCOMPARE(selection.indices(original), (QList<int>{0, 2}));
    QCOMPARE(notifications.size(), 0);
}

void ElementSelectionTests::deletingSelectedObjectDoesNotSelectItsReplacementIndex()
{
    auto document = documentWithElements();
    ElementSelection selection;
    selection.setIndices({0, 2}, document);
    const auto survivingId = document.elements[0].id;
    document.elements.removeAt(2);
    selection.reconcile(document);
    QCOMPARE(selection.ids(), QList<QUuid>{survivingId});
    QCOMPARE(selection.indices(document), QList<int>{0});
    QCOMPARE(selection.primaryId(), survivingId);
    document.elements.removeAt(0);
    selection.reconcile(document);
    QVERIFY(selection.ids().isEmpty());
    QVERIFY(selection.primaryId().isNull());
}

void ElementSelectionTests::groupExpansionKeepsRequestedPrimary()
{
    auto document = documentWithElements();
    const auto group = QUuid::createUuid();
    document.elements[0].groupId = group;
    document.elements[2].groupId = group;
    ElementSelection selection;
    selection.setIndices({3, 2}, document);
    QCOMPARE(selection.indices(document), (QList<int>{0, 3, 2}));
    QCOMPARE(selection.primaryId(), document.elements[2].id);
    const auto selected = selection.ids();
    document.elements.move(0, 3);
    selection.reconcile(document);
    QCOMPARE(selection.ids(), selected);
    QCOMPARE(selection.indices(document), (QList<int>{3, 2, 1}));
}

void ElementSelectionTests::invalidAndRepeatedSelectionsDoNotEmitChanges()
{
    const auto document = documentWithElements();
    ElementSelection selection;
    QSignalSpy notifications(&selection, &ElementSelection::selectionChanged);
    selection.setIndices({-1, 100}, document);
    selection.setIds({QUuid{}, QUuid::createUuid()}, document);
    QCOMPARE(notifications.size(), 0);
    selection.setIndices({2, 2, -1, 100}, document);
    QCOMPARE(selection.indices(document), QList<int>{2});
    QCOMPARE(notifications.size(), 1);
    selection.setIds({document.elements[2].id, document.elements[2].id}, document);
    selection.reconcile(document);
    QCOMPARE(notifications.size(), 1);
}

void ElementSelectionTests::sourceOnlySnapshotClearsSelection()
{
    auto document = documentWithElements();
    ElementSelection selection;
    selection.setIndices({1}, document);
    document.visualEditable = false;
    QVERIFY(selection.indices(document).isEmpty());
    selection.reconcile(document);
    QVERIFY(selection.ids().isEmpty());
    selection.setIds({document.elements[1].id}, document);
    QVERIFY(selection.ids().isEmpty());
}

QTEST_MAIN(ElementSelectionTests)
#include "test_element_selection.moc"
