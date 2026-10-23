#include "diagnostics_list.hpp"
#include "layers_list.hpp"
#include "document_session.hpp"
#include "element_selection.hpp"
#include "preview_coordinator.hpp"
#include "properties_panel.hpp"
#include "main_window.hpp"
#include "preview_widget.hpp"
#include "zpl_source_editor.hpp"

#include <QComboBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QtTest>

#include <optional>

using namespace Designer;

class EditorPanelsTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void layerSelectionUsesIdentityAfterReorderAndDelete();
    void primarySelectionAndLayerFlagsRemainVisible();
    void diagnosticActivationRequiresAnExactLocation();
    void propertiesEmitTypedRequestsWithoutMutatingSession();
    void codabarStartStopAreInspectorProperties();
    void mixedPropertiesKeepUnchangedValuesAbsent();
    void cachedPreviewReturnsWhenSourceDraftIsReverted();
    void sourceDiagnosticsNavigateNormalizedUnicodeText();
};

void EditorPanelsTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("QtZplDesignerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EditorPanels"));
}

void EditorPanelsTests::layerSelectionUsesIdentityAfterReorderAndDelete()
{
    LayersList list;
    DesignDocument document;
    document.elements = {makeElement(ElementType::Text), makeElement(ElementType::Box), makeElement(ElementType::Circle)};
    const auto first = document.elements[0].id;
    const auto last = document.elements[2].id;
    QSignalSpy changed(&list, &QListWidget::itemSelectionChanged);
    list.refresh(document, {first, last});
    QCOMPARE(list.primaryId(), last);
    QCOMPARE(list.selectedIds().last(), last);
    document.elements.swapItemsAt(0, 2);
    list.refresh(document, {first, last});
    QCOMPARE(list.primaryId(), last);
    QCOMPARE(list.currentItem()->data(Qt::UserRole).toInt(), 0);
    QVERIFY(list.selectedIds().contains(first));
    document.elements.removeFirst();
    list.refresh(document, {first, last});
    QCOMPARE(list.selectedIds(), QList<QUuid>{first});
    QCOMPARE(list.primaryId(), first);
    QCOMPARE(changed.count(), 0);
}

void EditorPanelsTests::primarySelectionAndLayerFlagsRemainVisible()
{
    LayersList list;
    DesignDocument document;
    auto element = makeElement(ElementType::Box);
    element.name = QStringLiteral("Mark");
    element.locked = true;
    element.editorVisible = false;
    element.printable = false;
    element.groupId = QUuid::createUuid();
    document.elements.append(element);
    list.refresh(document, {element.id});
    QCOMPARE(list.objectName(), QStringLiteral("layerList"));
    QVERIFY(list.item(0)->text().contains(QStringLiteral("Mark")));
    QVERIFY(list.item(0)->text().contains(QStringLiteral("🔒")));
    QVERIFY(list.item(0)->text().contains(QStringLiteral("◌")));
    QVERIFY(list.item(0)->text().contains(QStringLiteral("⊘")));
    QVERIFY(list.item(0)->text().contains(QStringLiteral("▣")));
    QCOMPARE(list.item(0)->data(Qt::UserRole + 1).toString(), QStringLiteral("скрыт"));
    list.setSelectedIds({});
    QVERIFY(list.selectedIds().isEmpty());
    QVERIFY(list.primaryId().isNull());
}

void EditorPanelsTests::diagnosticActivationRequiresAnExactLocation()
{
    SourceDiagnosticsList list;
    SourceDiagnostic field;
    field.text = QStringLiteral("Unsupported font");
    field.offset = 42;
    field.detail.code = QStringLiteral("external-font-preview");
    field.detail.offset = 42;
    field.detail.labelIndex = 1;
    field.detail.fieldId = 7;
    field.detail.sourceSpan = QtZpl::SourceSpan{42, 12};
    SourceDiagnostic general;
    general.text = QStringLiteral("Rendering failed");
    general.detail.severity = QtZpl::Severity::Error;
    general.detail.code = QStringLiteral("render-error");
    list.setDiagnostics({field, general});
    QCOMPARE(list.objectName(), QStringLiteral("sourceDiagnostics"));
    QCOMPARE(list.item(0)->data(Qt::UserRole + 1).toString(), field.detail.code);
    QCOMPARE(list.item(0)->data(Qt::UserRole + 3).toInt(), 1);
    QCOMPARE(list.item(0)->data(Qt::UserRole + 4).toLongLong(), 7);
    QCOMPARE(list.item(0)->data(Qt::UserRole + 5).toLongLong(), 42);
    QCOMPARE(list.item(0)->data(Qt::UserRole + 6).toLongLong(), 12);
    QSignalSpy activated(&list, &SourceDiagnosticsList::sourceActivated);
    list.itemActivated(list.item(1));
    QCOMPARE(activated.count(), 0);
    list.itemActivated(list.item(0));
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.first().first().toInt(), 42);
    list.setDiagnostics({});
    QCOMPARE(list.count(), 0);
    QVERIFY(list.isHidden());
}

void EditorPanelsTests::propertiesEmitTypedRequestsWithoutMutatingSession()
{
    DocumentSession session;
    DesignDocument document;
    document.elements.append(makeElement(ElementType::Box, {12, 18}));
    session.reset(document);
    ElementSelection selection;
    selection.setIds({document.elements.first().id}, document);
    PreviewCoordinator previews;
    previews.setSnapshot(document, session.revision());
    PropertiesPanel panel(&session, &previews, &selection);
    panel.refresh();
    std::optional<EditOperation> request;
    connect(&panel, &PropertiesPanel::editRequested, this,
        [&request](EditOperation operation, const QString&) { request = std::move(operation); });
    auto* x = panel.findChild<QSpinBox*>(QStringLiteral("elementX"));
    QVERIFY(x);
    x->setValue(42);
    QVERIFY(request.has_value());
    const auto* update = std::get_if<UpdateElementProperties>(&*request);
    QVERIFY(update);
    QCOMPARE(update->id, document.elements.first().id);
    QCOMPARE(update->properties.x, std::optional<int>{42});
    QCOMPARE(session.document().elements.first().x, 12);
    QCOMPARE(session.undoStack()->count(), 0);
}

void EditorPanelsTests::codabarStartStopAreInspectorProperties()
{
    DocumentSession session;
    DesignDocument document;
    document.elements.append(makeElement(ElementType::Codabar));
    session.reset(document);
    ElementSelection selection;
    selection.setIds({document.elements.first().id}, document);
    PreviewCoordinator previews;
    previews.setSnapshot(document, session.revision());
    PropertiesPanel panel(&session, &previews, &selection);
    panel.refresh();
    auto* start = panel.findChild<QComboBox*>(QStringLiteral("codabarStart"));
    auto* stop = panel.findChild<QComboBox*>(QStringLiteral("codabarStop"));
    QVERIFY(start && stop);
    QCOMPARE(start->currentData().toString(), QStringLiteral("A"));
    QCOMPARE(stop->currentData().toString(), QStringLiteral("B"));
    std::optional<EditOperation> request;
    connect(&panel, &PropertiesPanel::editRequested, this,
        [&request](EditOperation operation, const QString&) { request = std::move(operation); });
    stop->setCurrentIndex(stop->findData(QStringLiteral("D")));
    QVERIFY(request.has_value());
    const auto* update = std::get_if<UpdateElementProperties>(&*request);
    QVERIFY(update);
    QCOMPARE(update->properties.codabarStart, std::optional<QChar>{u'A'});
    QCOMPARE(update->properties.codabarStop, std::optional<QChar>{u'D'});
}

void EditorPanelsTests::mixedPropertiesKeepUnchangedValuesAbsent()
{
    DocumentSession session;
    DesignDocument document;
    document.elements = {makeElement(ElementType::Box), makeElement(ElementType::Box)};
    document.elements[0].thickness = 2;
    document.elements[1].thickness = 5;
    session.reset(document);
    ElementSelection selection;
    selection.setIds({document.elements[0].id, document.elements[1].id}, document);
    PreviewCoordinator previews;
    PropertiesPanel panel(&session, &previews, &selection);
    panel.refresh();
    auto* thickness = panel.findChild<QSpinBox*>(QStringLiteral("selection_thickness"));
    auto* apply = panel.findChild<QPushButton*>(QStringLiteral("applySelectionProperties"));
    QVERIFY(thickness);
    QVERIFY(apply);
    QCOMPARE(thickness->value(), 0);
    std::optional<EditOperation> request;
    connect(&panel, &PropertiesPanel::editRequested, this,
        [&request](EditOperation operation, const QString&) { request = std::move(operation); });
    apply->click();
    QVERIFY(request.has_value());
    auto* update = std::get_if<UpdateSelectionProperties>(&*request);
    QVERIFY(update);
    QVERIFY(!update->values.thickness);
    thickness->setValue(7);
    panel.refresh();
    apply->click();
    update = std::get_if<UpdateSelectionProperties>(&*request);
    QVERIFY(update);
    QCOMPARE(update->ids, selection.ids());
    QCOMPARE(update->values.thickness, std::optional<int>{7});
    QVERIFY(!update->values.fontHeight);
    QCOMPARE(session.document().elements[0].thickness, 2);
    QCOMPARE(session.document().elements[1].thickness, 5);
}

void EditorPanelsTests::cachedPreviewReturnsWhenSourceDraftIsReverted()
{
    MainWindow window;
    DesignDocument document;
    document.width = 100;
    document.height = 80;
    document.elements.append(makeElement(ElementType::Box, {12, 18}));
    window.setDocument(document);
    auto* layout = window.findChild<QComboBox*>(QStringLiteral("editorLayout"));
    QVERIFY(layout);
    layout->setCurrentIndex(0);
    auto* editor = window.findChild<ZplSourceEditor*>(QStringLiteral("zplEditor"));
    auto* exportPng = window.findChild<QPushButton*>(QStringLiteral("previewExportPng"));
    QVERIFY(editor);
    QVERIFY(exportPng);
    QVERIFY(exportPng->isEnabled());
    const auto applied = editor->toPlainText();
    editor->setPlainText(applied + QStringLiteral("draft"));
    QVERIFY(!exportPng->isEnabled());
    editor->setPlainText(applied);
    QVERIFY(exportPng->isEnabled());
}

void EditorPanelsTests::sourceDiagnosticsNavigateNormalizedUnicodeText()
{
    MainWindow window;
    DesignDocument document;
    document.width = 80;
    document.height = 50;
    document.visualEditable = false;
    document.originalZpl = QStringLiteral(
        "^XA^PW80^LL50^FXЖ😀^FS^XZ\r\n^XA^PW80^LL50^ZZretained^XZ");
    window.setDocument(document);
    auto* editor = window.findChild<ZplSourceEditor*>(QStringLiteral("zplEditor"));
    auto* diagnostics = window.findChild<SourceDiagnosticsList*>(QStringLiteral("sourceDiagnostics"));
    QVERIFY(editor);
    QVERIFY(diagnostics);
    const int offset = static_cast<int>(editor->toPlainText().indexOf(QStringLiteral("^ZZ")));
    QVERIFY(offset >= 0);
    QVERIFY(offset != document.originalZpl.indexOf(QStringLiteral("^ZZ")));
    QListWidgetItem* target = nullptr;
    for (int row = 0; row < diagnostics->count(); ++row)
        if (diagnostics->item(row)->data(Qt::UserRole).toInt() == offset) target = diagnostics->item(row);
    QVERIFY(target);
    diagnostics->itemActivated(target);
    QCOMPARE(editor->textCursor().position(), offset);
}

QTEST_MAIN(EditorPanelsTests)
#include "test_editor_panels.moc"
