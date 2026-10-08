#include "designer_theme.hpp"
#include "main_window.hpp"
#include "workspace_preferences.hpp"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QStandardPaths>
#include <QUuid>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

class WindowGeometryTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void positionAndSizeRestoreAcrossWindows();
    void maximizedWindowRetainsNormalGeometry();
    void invalidGeometryKeepsDefaultSize();

private:
    QString previousName_;
    QString settingsDirectory_;
    bool previousTestMode_ = false;
};

void WindowGeometryTests::init()
{
    previousName_ = QCoreApplication::applicationName();
    previousTestMode_ = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(u"window-geometry-tests-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    settingsDirectory_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    installDesignerStyle();
    setDesignerTheme(DesignerTheme::Light);
}

void WindowGeometryTests::cleanup()
{
    QFile::remove(workspacePreferencesPath());
    QDir().rmdir(settingsDirectory_);
    QCoreApplication::setApplicationName(previousName_);
    QStandardPaths::setTestModeEnabled(previousTestMode_);
}

void WindowGeometryTests::positionAndSizeRestoreAcrossWindows()
{
    QByteArray saved;
    {
        MainWindow original;
        original.resize(1190, 730);
        original.move(24, 28);
        original.show();
        QApplication::processEvents();
        saved = original.saveGeometry();
        QVERIFY(original.close());
    }
    QCOMPARE(loadWorkspacePreferences().windowGeometry, saved);

    // Use Qt's screen adjustment as the reference: the offscreen test platform
    // can have a smaller virtual screen than the editor's minimum width.
    QMainWindow expected;
    expected.setMinimumSize(1050, 680);
    QVERIFY(expected.restoreGeometry(saved));
    MainWindow restored;
    QCOMPARE(restored.geometry(), expected.geometry());
    QVERIFY(!restored.isMaximized());
    QVERIFY(!restored.isMinimized());
}

void WindowGeometryTests::maximizedWindowRetainsNormalGeometry()
{
    QByteArray saved;
    {
        MainWindow original;
        original.resize(1170, 710);
        original.move(30, 36);
        original.show();
        QApplication::processEvents();
        original.showMaximized();
        QApplication::processEvents();
        QVERIFY(original.isMaximized());
        saved = original.saveGeometry();
        QVERIFY(original.close());
    }
    QCOMPARE(loadWorkspacePreferences().windowGeometry, saved);

    QMainWindow expected;
    expected.setMinimumSize(1050, 680);
    QVERIFY(expected.restoreGeometry(saved));
    MainWindow restored;
    QVERIFY(restored.isMaximized());
    QCOMPARE(restored.normalGeometry(), expected.normalGeometry());
    restored.show();
    QApplication::processEvents();
    QVERIFY(restored.isMaximized());
    QVERIFY(!restored.isMinimized());
}

void WindowGeometryTests::invalidGeometryKeepsDefaultSize()
{
    WorkspacePreferences preferences;
    preferences.windowGeometry = "not a Qt window geometry";
    QVERIFY(saveWorkspacePreferences(preferences));
    MainWindow window;
    QCOMPARE(window.size(), QSize(1440, 900));
    QVERIFY(!window.isMaximized());
    QVERIFY(!window.isMinimized());
}

QTEST_MAIN(WindowGeometryTests)
#include "test_window_geometry.moc"
