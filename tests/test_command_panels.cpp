#include "designer_theme.hpp"
#include "document_session.hpp"
#include "main_window.hpp"
#include "workspace_preferences.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QtTest>

#include <memory>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
const QStringList layoutCommands{
    u"alignLeft"_s, u"alignHorizontalCenter"_s, u"alignRight"_s,
    u"alignTop"_s, u"alignVerticalCenter"_s, u"alignBottom"_s,
    u"distributeHorizontal"_s, u"distributeVertical"_s
};
const QStringList groupingCommands{u"groupElements"_s, u"ungroupElements"_s};
const QStringList editCommands{
    u"cutElements"_s, u"copyElements"_s, u"pasteElements"_s,
    u"duplicateElements"_s, u"deleteElements"_s
};

QStringList visibleCommands(const QToolBar* panel)
{
    QStringList result;
    for (const auto* action : panel->actions()) {
        if (action->isVisible())
            result.append(action->isSeparator() ? u"|"_s : action->objectName());
    }
    return result;
}

QStringList proxyNames(const QStringList& names)
{
    QStringList result;
    for (const auto& name : names) result.append(name + u"Toolbar");
    return result;
}
}

class CommandPanelTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void defaultOrderIconsAndBrandSpacing();
    void individualVisibilityPersistsAndSeparatorsFollowGroups();
    void themePreservesGeometryAndUpdatesIcons();
    void undoButtonKeepsSourceFocusAndNativeHistory();
    void singleCodeUndoKeepsNativeHistory();
    void optionalCommandPanelScreenshots();

private:
    void createWindow();
    QAction* action(const QString& name) const;
    QToolButton* button(QToolBar* panel, const QString& canonicalName) const;
    std::unique_ptr<MainWindow> window_;
    DesignDocument initial_;
    QString previousName_;
    QString settingsDirectory_;
    bool previousTestMode_ = false;
};

QAction* CommandPanelTests::action(const QString& name) const
{
    return window_->findChild<QAction*>(name);
}

QToolButton* CommandPanelTests::button(QToolBar* panel, const QString& canonicalName) const
{
    auto* proxy = action(canonicalName + u"Toolbar");
    return proxy ? qobject_cast<QToolButton*>(panel->widgetForAction(proxy)) : nullptr;
}

void CommandPanelTests::createWindow()
{
    window_ = std::make_unique<MainWindow>();
    window_->setDocument(initial_);
    window_->resize(1440, 900);
    window_->show();
    QApplication::processEvents();
}

void CommandPanelTests::init()
{
    previousName_ = QCoreApplication::applicationName();
    previousTestMode_ = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(u"command-panel-tests-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    settingsDirectory_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    installDesignerStyle();
    setDesignerTheme(DesignerTheme::Light);
    initial_ = {};
    initial_.width = 400;
    initial_.height = 300;
    auto box = makeElement(ElementType::Box, {40, 48});
    box.width = 80;
    box.height = 56;
    initial_.elements = {box};
    createWindow();
}

void CommandPanelTests::cleanup()
{
    if (window_) {
        window_->setDocument(initial_);
        window_.reset();
    }
    QFile::remove(workspacePreferencesPath());
    QDir().rmdir(settingsDirectory_);
    QCoreApplication::setApplicationName(previousName_);
    QStandardPaths::setTestModeEnabled(previousTestMode_);
    setDesignerTheme(DesignerTheme::Light);
}

void CommandPanelTests::defaultOrderIconsAndBrandSpacing()
{
    auto* documentPanel = window_->findChild<QToolBar*>(u"documentCommandPanel"_s);
    auto* editPanel = window_->findChild<QToolBar*>(u"editCommandPanel"_s);
    auto* brand = window_->findChild<QLabel*>(u"applicationBrand"_s);
    QVERIFY(documentPanel); QVERIFY(editPanel); QVERIFY(brand);
    QCOMPARE(visibleCommands(documentPanel), (QStringList{
        u"saveActionToolbar"_s, u"|"_s, u"undoActionToolbar"_s,
        u"redoActionToolbar"_s, u"|"_s, u"printActionToolbar"_s}));
    QCOMPARE(visibleCommands(editPanel), proxyNames(layoutCommands) + QStringList{u"|"_s}
        + proxyNames(groupingCommands) + QStringList{u"|"_s} + proxyNames(editCommands));

    int buttonCount = 0;
    for (auto* panel : {documentPanel, editPanel}) {
        for (auto* proxy : panel->actions()) {
            if (proxy->isSeparator()) continue;
            auto* tool = qobject_cast<QToolButton*>(panel->widgetForAction(proxy));
            QVERIFY(tool);
            QVERIFY(tool->isVisible());
            QCOMPARE(tool->toolButtonStyle(), Qt::ToolButtonIconOnly);
            QCOMPARE(tool->focusPolicy(), Qt::NoFocus);
            QVERIFY(!tool->icon().isNull());
            const auto canonicalName = proxy->objectName().chopped(7);
            auto* original = action(canonicalName);
            auto* toggle = action(u"visibility_" + canonicalName);
            QVERIFY(original); QVERIFY(toggle);
            QVERIFY(toggle->isCheckable());
            QVERIFY(toggle->isChecked());
            QVERIFY(tool->toolTip().contains(original->text().remove(u'&')));
            if (!original->shortcut().isEmpty())
                QVERIFY(tool->toolTip().contains(original->shortcut().toString(QKeySequence::NativeText)));
            ++buttonCount;
        }
    }
    QCOMPARE(buttonCount, 19);
    auto* save = button(documentPanel, u"saveAction"_s);
    QVERIFY(save);
    const int brandRight = brand->mapTo(window_.get(), QPoint(brand->width(), 0)).x();
    const int buttonLeft = save->mapTo(window_.get(), QPoint{}).x();
    QCOMPARE(buttonLeft - brandRight, 24);
}

void CommandPanelTests::individualVisibilityPersistsAndSeparatorsFollowGroups()
{
    auto* documentPanel = window_->findChild<QToolBar*>(u"documentCommandPanel"_s);
    auto* editPanel = window_->findChild<QToolBar*>(u"editCommandPanel"_s);
    auto* documentMenu = window_->findChild<QMenu*>(u"documentCommandVisibilityMenu"_s);
    auto* editMenu = window_->findChild<QMenu*>(u"editCommandVisibilityMenu"_s);
    QVERIFY(documentPanel); QVERIFY(editPanel); QVERIFY(documentMenu); QVERIFY(editMenu);
    auto* saveToggle = action(u"visibility_saveAction"_s);
    auto* saveProxy = action(u"saveActionToolbar"_s);
    auto* saveOriginal = action(u"saveAction"_s);
    QVERIFY(saveToggle); QVERIFY(saveProxy); QVERIFY(saveOriginal);
    QVERIFY(documentMenu->actions().contains(saveToggle));

    bool contextMenuVisible = false;
    QTimer::singleShot(0, documentMenu, [&] {
        contextMenuVisible = documentMenu->isVisible();
        saveToggle->trigger();
        documentMenu->close();
    });
    const auto position = documentPanel->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, documentPanel->mapToGlobal(position));
    QApplication::sendEvent(documentPanel, &event);
    QApplication::processEvents();
    QVERIFY(contextMenuVisible);
    QVERIFY(!saveToggle->isChecked());
    QVERIFY(!saveProxy->isVisible());
    QVERIFY(saveOriginal->isVisible());
    QCOMPARE(loadWorkspacePreferences().hiddenToolbarCommands, QStringList{u"saveAction"_s});
    QCOMPARE(visibleCommands(documentPanel), (QStringList{
        u"undoActionToolbar"_s, u"redoActionToolbar"_s, u"|"_s, u"printActionToolbar"_s}));

    for (const auto& name : groupingCommands) {
        auto* toggle = action(u"visibility_" + name);
        QVERIFY(toggle);
        QVERIFY(editMenu->actions().contains(toggle));
        toggle->trigger();
        QVERIFY(action(name)->isVisible());
    }
    QCOMPARE(visibleCommands(editPanel), proxyNames(layoutCommands)
        + QStringList{u"|"_s} + proxyNames(editCommands));
    for (const auto& name : layoutCommands) action(u"visibility_" + name)->trigger();
    QCOMPARE(visibleCommands(editPanel), proxyNames(editCommands));

    const auto hidden = loadWorkspacePreferences().hiddenToolbarCommands;
    window_.reset();
    createWindow();
    QCOMPARE(loadWorkspacePreferences().hiddenToolbarCommands, hidden);
    documentPanel = window_->findChild<QToolBar*>(u"documentCommandPanel"_s);
    editPanel = window_->findChild<QToolBar*>(u"editCommandPanel"_s);
    QVERIFY(documentPanel); QVERIFY(editPanel);
    for (const auto& name : hidden) {
        QVERIFY(!action(name + u"Toolbar")->isVisible());
        QVERIFY(!action(u"visibility_" + name)->isChecked());
        QVERIFY(action(name)->isVisible());
    }
    QCOMPARE(visibleCommands(editPanel), proxyNames(editCommands));
    action(u"visibility_saveAction"_s)->trigger();
    QVERIFY(action(u"saveActionToolbar"_s)->isVisible());
    QVERIFY(!loadWorkspacePreferences().hiddenToolbarCommands.contains(u"saveAction"_s));
}

void CommandPanelTests::themePreservesGeometryAndUpdatesIcons()
{
    auto* documentPanel = window_->findChild<QToolBar*>(u"documentCommandPanel"_s);
    auto* editPanel = window_->findChild<QToolBar*>(u"editCommandPanel"_s);
    auto* themeAction = action(u"darkTheme"_s);
    QVERIFY(documentPanel); QVERIFY(editPanel); QVERIFY(themeAction);
    auto* save = button(documentPanel, u"saveAction"_s);
    QVERIFY(save);
    auto* style = QApplication::style();
    const auto font = QApplication::font();
    const auto documentGeometry = documentPanel->geometry();
    const auto editGeometry = editPanel->geometry();
    QList<QPair<QToolButton*, QRect>> buttonGeometries;
    for (auto* panel : {documentPanel, editPanel}) {
        for (auto* proxy : panel->actions()) {
            if (auto* tool = qobject_cast<QToolButton*>(panel->widgetForAction(proxy)))
                buttonGeometries.append({tool, tool->geometry()});
        }
    }
    const auto lightIcon = save->icon().pixmap(save->iconSize()).toImage();
    QVERIFY(!lightIcon.isNull());
    themeAction->trigger();
    QApplication::processEvents();
    QCOMPARE(designerTheme(), DesignerTheme::Dark);
    QCOMPARE(QApplication::style(), style);
    QCOMPARE(QApplication::font(), font);
    QCOMPARE(documentPanel->geometry(), documentGeometry);
    QCOMPARE(editPanel->geometry(), editGeometry);
    for (const auto& [tool, geometry] : buttonGeometries) QCOMPARE(tool->geometry(), geometry);
    const auto darkIcon = save->icon().pixmap(save->iconSize()).toImage();
    QVERIFY(!darkIcon.isNull());
    QVERIFY(darkIcon != lightIcon);
    themeAction->trigger();
    QApplication::processEvents();
    QCOMPARE(save->icon().pixmap(save->iconSize()).toImage(), lightIcon);
}

void CommandPanelTests::undoButtonKeepsSourceFocusAndNativeHistory()
{
    auto* documentPanel = window_->findChild<QToolBar*>(u"documentCommandPanel"_s);
    auto* layout = window_->findChild<QComboBox*>(u"editorLayout"_s);
    auto* code = window_->findChild<QPlainTextEdit*>(u"zplEditor"_s);
    auto* session = window_->findChild<DocumentSession*>();
    QVERIFY(documentPanel); QVERIFY(layout); QVERIFY(code); QVERIFY(session);
    auto* undo = button(documentPanel, u"undoAction"_s);
    auto* redo = button(documentPanel, u"redoAction"_s);
    QVERIFY(undo); QVERIFY(redo);
    QVERIFY(session->moveElements({0}, {4, 0}));
    layout->setCurrentIndex(1);
    code->setFocus();
    QTRY_VERIFY(code->hasFocus());
    const auto original = code->toPlainText();
    const int offset = static_cast<int>(original.indexOf(u"^FO44,48"));
    QVERIFY(offset >= 0);
    QTextCursor cursor(code->document());
    cursor.setPosition(offset + 3);
    cursor.setPosition(offset + 5, QTextCursor::KeepAnchor);
    cursor.insertText(u"68"_s);
    code->setTextCursor(cursor);
    const auto entered = code->toPlainText();
    QTRY_VERIFY_WITH_TIMEOUT(!session->hasPendingSource(), 2500);
    QCOMPARE(window_->document().elements.first().x, 68);
    QVERIFY(undo->isEnabled());
    const int sessionHistoryIndex = session->undoStack()->index();
    QTest::mouseClick(undo, Qt::LeftButton);
    QVERIFY(code->hasFocus());
    QCOMPARE(code->toPlainText(), original);
    QCOMPARE(session->undoStack()->index(), sessionHistoryIndex);
    QTRY_VERIFY_WITH_TIMEOUT(!session->hasPendingSource(), 2500);
    QCOMPARE(window_->document().elements.first().x, 44);
    QVERIFY(redo->isEnabled());
    QTest::mouseClick(redo, Qt::LeftButton);
    QVERIFY(code->hasFocus());
    QCOMPARE(code->toPlainText(), entered);
    QTRY_VERIFY_WITH_TIMEOUT(!session->hasPendingSource(), 2500);
    QCOMPARE(window_->document().elements.first().x, 68);
}

void CommandPanelTests::singleCodeUndoKeepsNativeHistory()
{
    auto* documentPanel = window_->findChild<QToolBar*>(u"documentCommandPanel"_s);
    auto* tabs = window_->findChild<QTabWidget*>(u"editorTabs"_s);
    auto* code = window_->findChild<QPlainTextEdit*>(u"zplEditor"_s);
    auto* session = window_->findChild<DocumentSession*>();
    QVERIFY(documentPanel); QVERIFY(tabs); QVERIFY(code); QVERIFY(session);
    auto* undo = button(documentPanel, u"undoAction"_s);
    auto* redo = button(documentPanel, u"redoAction"_s);
    QVERIFY(undo); QVERIFY(redo);
    QVERIFY(session->moveElements({0}, {4, 0}));
    tabs->setCurrentIndex(1);
    code->setFocus();
    QTRY_VERIFY(code->hasFocus());
    const auto original = code->toPlainText();
    const int sessionHistoryIndex = session->undoStack()->index();
    const int offset = static_cast<int>(original.indexOf(u"^FO44,48"));
    QVERIFY(offset >= 0);
    QTextCursor cursor(code->document());
    cursor.setPosition(offset + 3);
    cursor.setPosition(offset + 5, QTextCursor::KeepAnchor);
    cursor.insertText(u"68"_s);
    code->setTextCursor(cursor);
    const auto entered = code->toPlainText();
    QVERIFY(session->hasPendingSource());
    QVERIFY(undo->isEnabled());
    QTest::mouseClick(undo, Qt::LeftButton);
    QVERIFY(code->hasFocus());
    QCOMPARE(code->toPlainText(), original);
    QCOMPARE(session->undoStack()->index(), sessionHistoryIndex);
    QCOMPARE(window_->document().elements.first().x, 44);
    QVERIFY(redo->isEnabled());
    QTest::mouseClick(redo, Qt::LeftButton);
    QVERIFY(code->hasFocus());
    QCOMPARE(code->toPlainText(), entered);
    QVERIFY(session->hasPendingSource());
    QCOMPARE(session->undoStack()->index(), sessionHistoryIndex);
    QCOMPARE(window_->document().elements.first().x, 44);
}

void CommandPanelTests::optionalCommandPanelScreenshots()
{
    const auto destination = qEnvironmentVariable("DESIGNER_TOOLBAR_SCREENSHOTS");
    if (destination.isEmpty()) return;
    QVERIFY(QDir().mkpath(destination));
    const QDir directory(destination);
    const auto capture = [&](const QString& name) {
        QTest::qWait(150);
        QApplication::processEvents();
        QVERIFY(window_->grab().save(directory.filePath(name + u".png")));
    };
    capture(u"toolbar-light"_s);
    action(u"darkTheme"_s)->trigger();
    capture(u"toolbar-dark"_s);
    window_->resize(window_->minimumSize());
    capture(u"toolbar-minimum"_s);
    action(u"darkTheme"_s)->trigger();
    for (const auto& name : QStringList{u"saveAction"_s, u"groupElements"_s, u"ungroupElements"_s})
        action(u"visibility_" + name)->trigger();
    capture(u"toolbar-hidden"_s);
}

QTEST_MAIN(CommandPanelTests)
#include "test_command_panels.moc"
