#include "design_document.hpp"
#include "document_session.hpp"
#include "eac_mark.hpp"
#include "element_clipboard.hpp"
#include "element_selection.hpp"
#include "main_window.hpp"
#include "preview_coordinator.hpp"
#include "project_codec.hpp"
#include "properties_panel.hpp"
#include "zpl_source_map.hpp"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QListWidget>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <array>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
constexpr std::array styles{EacStyle::BlackOnWhite, EacStyle::WhiteOnBlack,
    EacStyle::BlackTransparent, EacStyle::WhiteTransparent};

DesignDocument referenceDocument(EacStyle style)
{
    DesignDocument document;
    document.width = document.height = 160;
    auto background = makeElement(ElementType::Box, {0, 0});
    background.width = 60; background.height = 160; background.thickness = 160;
    auto mark = makeElement(ElementType::Eac, {20, 20});
    mark.eacStyle = style;
    mark.width = mark.height = style == EacStyle::WhiteOnBlack ? 110 : 90;
    document.elements = {background, mark};
    return document;
}

QString fixtureName(EacStyle style)
{
    switch (style) {
    case EacStyle::BlackOnWhite: return u"black-on-white"_s;
    case EacStyle::WhiteOnBlack: return u"white-on-black"_s;
    case EacStyle::BlackTransparent: return u"black-transparent"_s;
    case EacStyle::WhiteTransparent: return u"white-transparent"_s;
    }
    return {};
}
}

class EacTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void normativeGridAndInverseMargin();
    void matchesLabelary_data();
    void matchesLabelary();
    void editableRoundTrips_data();
    void editableRoundTrips();
    void physicalMinimumAndUniformResize();
    void dpiRoundTripPasteAndAlignment();
    void invalidGeometryCannotExport();
    void stylePropertiesAndUndo();
    void paletteAndDefaultLabel();
};

void EacTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(u"QtZplDesignerTests"_s);
    QCoreApplication::setApplicationName(u"Eac"_s);
}

void EacTests::normativeGridAndInverseMargin()
{
    // Independent transcription of decision 711, figures 3/4, EEC source:
    // https://eec.eaeunion.org/upload/medialibrary/986/PologenieEAC.pdf
    constexpr std::array<const char*, 9> mask{
        "##.###.##", "#..#.#.#.", "#..#.#.#.", "#..#.#.#.", "##.###.#.",
        "#..#.#.#.", "#..#.#.#.", "#..#.#.#.", "##.#.#.##"};
    for (const auto style : {EacStyle::BlackOnWhite, EacStyle::WhiteOnBlack}) {
        auto document = referenceDocument(style);
        document.elements.removeFirst();
        const auto preview = renderPreview(document);
        QVERIFY2(preview.has_value(), preview ? "" : qPrintable(preview.error()));
        const bool inverse = style == EacStyle::WhiteOnBlack;
        const int size = inverse ? 110 : 90;
        const int margin = inverse ? 10 : 0;
        QCOMPARE(preview->bounds.first(), QRect(20, 20, size, size));
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const bool inside = x >= margin && y >= margin && x < margin + 90 && y < margin + 90;
                const bool glyph = inside && mask[(y - margin) / 10][(x - margin) / 10] == '#';
                const bool black = inverse ? !glyph : glyph;
                QCOMPARE(qGray(preview->image.pixel(20 + x, 20 + y)), black ? 0 : 255);
            }
        }
    }
}

void EacTests::matchesLabelary_data()
{
    QTest::addColumn<int>("styleValue");
    for (auto style : styles) QTest::newRow(qPrintable(fixtureName(style))) << int(style);
}

void EacTests::matchesLabelary()
{
    QFETCH(int, styleValue);
    const auto style = EacStyle(styleValue);
    const auto path = QFINDTESTDATA(qPrintable(u"fixtures/eac/%1-labelary.png"_s.arg(fixtureName(style))));
    QVERIFY(!path.isEmpty());
    const QImage golden(path);
    QVERIFY(!golden.isNull());
    const auto rendered = renderPreview(referenceDocument(style));
    QVERIFY2(rendered.has_value(), rendered ? "" : qPrintable(rendered.error()));
    QCOMPARE(rendered->image.convertToFormat(QImage::Format_RGB32), golden.convertToFormat(QImage::Format_RGB32));
}

void EacTests::editableRoundTrips_data()
{
    matchesLabelary_data();
}

void EacTests::editableRoundTrips()
{
    QFETCH(int, styleValue);
    const auto style = EacStyle(styleValue);
    auto document = referenceDocument(style);
    auto following = makeElement(ElementType::Box, {135, 130});
    following.width = following.height = 10;
    document.elements.append(following);
    document.elements[1].name = u"Знак соответствия"_s;
    document.elements[1].groupId = QUuid::createUuid();
    const auto encoded = encodeElements({document.elements[1]});
    const auto clipboard = decodeElements(encoded);
    QVERIFY(clipboard.has_value());
    QCOMPARE(clipboard->first().type, ElementType::Eac);
    QCOMPARE(clipboard->first().eacStyle, style);
    QCOMPARE(clipboard->first().groupId, document.elements[1].groupId);

    const auto project = encodeProject({document, {}, {}});
    QVERIFY2(project.has_value(), project ? "" : qPrintable(project.error()));
    const auto restoredProject = decodeProject(*project);
    QVERIFY(restoredProject.has_value());
    QCOMPARE(restoredProject->document.elements[1].eacStyle, style);

    const auto zpl = toZpl(document);
    const auto imported = fromZpl(zpl);
    QVERIFY2(imported.has_value(), imported ? "" : qPrintable(imported.error()));
    QVERIFY2(imported->visualEditable, qPrintable(imported->importNotes.join(u'\n')));
    QCOMPARE(imported->elements.size(), 3);
    QCOMPARE(imported->elements[1].type, ElementType::Eac);
    QCOMPARE(imported->elements[1].eacStyle, style);
    QCOMPARE(imported->elements[1].name, document.elements[1].name);
    QCOMPARE(toZpl(*imported), zpl);
    const auto ranges = sourceElementRanges(*imported, zpl);
    QVERIFY(!ranges.isEmpty());
    QCOMPARE(ranges.first().elementIndex, 0);
    QCOMPARE(ranges.last().elementIndex, 2);
    QVERIFY(std::ranges::count_if(ranges, [](const auto& range) { return range.elementIndex == 1; }) > 1);

    auto moved = *imported;
    ElementProperties properties;
    properties.x = 25;
    const auto operation = applyOperation(moved, UpdateElementProperties{moved.elements[1].id, properties});
    QVERIFY(operation && *operation);
    const auto reopened = fromZpl(toZpl(moved));
    QVERIFY(reopened && reopened->visualEditable);
    QCOMPARE(reopened->elements[1].x, 25);
    QCOMPARE(reopened->elements[1].eacStyle, style);
    const auto before = renderPreview(moved);
    const auto after = renderPreview(*reopened);
    QVERIFY(before && after);
    QCOMPARE(before->image, after->image);
}

void EacTests::physicalMinimumAndUniformResize()
{
    for (const int dpi : {203, 300, 600}) {
        for (const auto style : styles) {
            DesignDocument document;
            document.dpi = dpi;
            auto mark = makeElement(ElementType::Eac);
            mark.eacStyle = style;
            mark.width = mark.height = 1;
            const auto added = applyOperation(document, AddElement{mark});
            QVERIFY2(added.has_value(), added ? "" : qPrintable(added.error()));
            const auto& current = document.elements.first();
            const int grid = style == EacStyle::WhiteOnBlack ? 11 : 9;
            QCOMPARE(current.width, current.height);
            QCOMPARE(current.width % grid, 0);
            QVERIFY((current.width / grid * 9) * 25.4 / dpi >= 5.0);
            QCOMPARE(current.width, eacMinimumSize(dpi, style));
            const auto resized = applyOperation(document, ResizeElement{current.id, {1, 35}, {current.width, current.height}, {}, {}});
            QVERIFY(resized.has_value());
            QCOMPARE(document.elements.first().width, document.elements.first().height);
            QVERIFY(document.elements.first().width >= eacMinimumSize(dpi, style));
        }
    }
    auto document = referenceDocument(EacStyle::BlackOnWhite);
    UpdateLabelSettings settings{document.width, document.height, 600};
    const auto changedDpi = applyOperation(document, settings);
    QVERIFY(changedDpi && *changedDpi);
    QVERIFY(document.elements[1].width >= eacMinimumSize(600, EacStyle::BlackOnWhite));
}

void EacTests::invalidGeometryCannotExport()
{
    auto document = referenceDocument(EacStyle::BlackOnWhite);
    document.elements[1].height = 80;
    QVERIFY(!validateForExport(document));
    document.elements[1].width = document.elements[1].height = 9;
    QVERIFY(!validateForExport(document));
    document.elements[1].width = document.elements[1].height = 90;
    document.elements[1].eacStyle = EacStyle(100);
    QVERIFY(!validateForExport(document));
}

void EacTests::dpiRoundTripPasteAndAlignment()
{
    auto source = referenceDocument(EacStyle::WhiteOnBlack);
    source.elements.removeFirst();
    source.elements[0].width = source.elements[0].height = eacMinimumSize(203, EacStyle::WhiteOnBlack);
    DesignDocument destination;
    destination.dpi = 600;
    DocumentSession session;
    session.reset(destination);
    QCOMPARE(session.pasteElements(source.elements, {0, 0}), QList<int>{0});
    QCOMPARE(session.document().elements[0].width, eacMinimumSize(600, EacStyle::WhiteOnBlack));
    const auto saved = toZpl(session.document());
    const auto imported = fromZpl(saved);
    QVERIFY(imported && imported->visualEditable);
    QCOMPARE(imported->dpi, 600);
    QCOMPARE(imported->elements[0].eacStyle, EacStyle::WhiteOnBlack);
    QCOMPARE(toZpl(*imported), saved);
    QTemporaryDir directory(QDir(QCoreApplication::applicationDirPath()).filePath(u"eac-XXXXXX"_s));
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(u"portable.zpl"_s);
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const auto bytes = saved.toUtf8();
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
    DocumentSession opened;
    QVERIFY(opened.open(path));
    QCOMPARE(opened.document().dpi, 600);
    DocumentSession applied;
    applied.setPendingSource(saved);
    QVERIFY(applied.applySource());
    QCOMPARE(applied.document().dpi, 600);
    session.setPendingSource(toZpl(source));
    QVERIFY(session.applySource());
    QCOMPARE(session.document().dpi, 203);
    session.undoStack()->undo();
    QCOMPARE(session.document().dpi, 600);

    auto overflowing = session.document();
    overflowing.width = 300;
    overflowing.elements[0].x = 260;
    overflowing.defaultJustification = 1;
    session.reset(overflowing);
    const auto preview = renderPreview(overflowing);
    QVERIFY(preview);
    QVERIFY(session.alignElements({0}, ElementAlignment::HorizontalCenter, AlignmentReference::Label, preview->bounds));
    QCOMPARE(session.document().elements[0].x, (300 - overflowing.elements[0].width) / 2);
}

void EacTests::stylePropertiesAndUndo()
{
    DocumentSession session;
    auto document = referenceDocument(EacStyle::BlackOnWhite);
    session.reset(document);
    ElementSelection selection;
    selection.setIndices({1}, document);
    PreviewCoordinator previews;
    previews.setSnapshot(document, session.revision());
    PropertiesPanel panel(&session, &previews, &selection);
    connect(&panel, &PropertiesPanel::editRequested, &session, [&](const EditOperation& operation, const QString& title) {
        const auto result = session.execute(operation, title);
        QVERIFY2(result.has_value(), result ? "" : qPrintable(result.error()));
    });
    panel.refresh();
    auto* style = panel.findChild<QComboBox*>(u"eacStyle"_s);
    auto* width = panel.findChild<QSpinBox*>(u"elementWidth"_s);
    QVERIFY(style && width);
    QCOMPARE(style->count(), 4);
    QVERIFY(!style->isHidden());
    QCOMPARE(width->minimum(), eacMinimumSize(203, EacStyle::BlackOnWhite));
    style->setCurrentIndex(style->findData(int(EacStyle::WhiteOnBlack)));
    QCOMPARE(session.document().elements[1].eacStyle, EacStyle::WhiteOnBlack);
    QCOMPARE(session.undoStack()->count(), 1);
    session.undoStack()->undo();
    QCOMPARE(session.document().elements[1].eacStyle, EacStyle::BlackOnWhite);
    session.undoStack()->redo();
    QCOMPARE(session.document().elements[1].eacStyle, EacStyle::WhiteOnBlack);
}

void EacTests::paletteAndDefaultLabel()
{
    MainWindow window;
    DesignDocument empty;
    empty.dpi = 600;
    window.setDocument(empty);
    auto* palette = window.findChild<QListWidget*>(u"elementPalette"_s);
    QVERIFY(palette);
    QListWidgetItem* eacItem = nullptr;
    for (int i = 0; i < palette->count(); ++i)
        if (palette->item(i)->data(Qt::UserRole).toInt() == int(ElementType::Eac)) eacItem = palette->item(i);
    QVERIFY(eacItem);
    QVERIFY(!eacItem->icon().isNull());
    palette->itemDoubleClicked(eacItem);
    QCOMPARE(window.document().elements.size(), 1);
    QCOMPARE(window.document().elements.first().type, ElementType::Eac);
    QVERIFY(window.document().elements.first().width >= eacMinimumSize(600, EacStyle::BlackOnWhite));

    const auto sample = sampleDocument();
    const auto eac = std::ranges::find_if(sample.elements, [](const Element& element) { return element.type == ElementType::Eac; });
    QVERIFY(eac != sample.elements.end());
    QVERIFY(eac->width * 25.4 / sample.dpi >= 5.0);
    const auto rendered = renderPreview(sample);
    QVERIFY2(rendered.has_value(), rendered ? "" : qPrintable(rendered.error()));
    const QString artifacts = qEnvironmentVariable("DESIGNER_EAC_ARTIFACTS");
    if (!artifacts.isEmpty()) {
        QVERIFY(QDir().mkpath(artifacts));
        QVERIFY(rendered->image.save(QDir(artifacts).filePath(u"default-label.png"_s)));
        window.setDocument(sample);
        window.show();
        QTest::qWait(250);
        QVERIFY(window.grab().save(QDir(artifacts).filePath(u"default-window.png"_s)));
    }
}

QTEST_MAIN(EacTests)
#include "test_eac.moc"
