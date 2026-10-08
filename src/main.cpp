#include "main_window.hpp"
#include "app_version.hpp"
#include "ui_translations.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    if (!Designer::installRussianUiTranslations()) {
        qCritical("Не удалось загрузить встроенный русский перевод интерфейса.");
        return 1;
    }
    application.setApplicationName(QStringLiteral("QtZpl Designer"));
    application.setOrganizationName(QStringLiteral("QtZpl"));
    application.setApplicationVersion(QString::fromLatin1(Designer::applicationVersion));
    Designer::installDesignerStyle();
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native visual ZPL label editor"));
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("Exercise document save/open and exit.")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("Save the initial editor screenshot and exit."), QStringLiteral("path")});
    parser.addOption({QStringLiteral("preview"), QStringLiteral("Open the clean label preview tab on launch.")});
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("ZPL or .qtzpl project file to open."), QStringLiteral("[file]"));
    parser.process(application);
    Designer::MainWindow window;
    if (!parser.positionalArguments().isEmpty() && !window.openFile(parser.positionalArguments().first())) return 2;
    window.show();
    if (parser.positionalArguments().isEmpty() && !parser.isSet(QStringLiteral("smoke-test"))
        && !parser.isSet(QStringLiteral("screenshot"))) {
        QTimer::singleShot(0, &window, [&window] { window.showRecoveryDialog(true); });
    }
    if (parser.isSet(QStringLiteral("preview"))) {
        window.showPreview();
    }
    if (parser.isSet(QStringLiteral("smoke-test")) || parser.isSet(QStringLiteral("screenshot"))) {
        QTimer::singleShot(250, &window, [&] {
            bool success = true;
            if (parser.isSet(QStringLiteral("smoke-test"))) {
                success = window.smokeTest(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("smoke-output")));
                window.setDocument(Designer::sampleDocument());
            }
            if (parser.isSet(QStringLiteral("screenshot"))) success = window.grab().save(parser.value(QStringLiteral("screenshot"))) && success;
            application.exit(success ? 0 : 3);
        });
    }
    return application.exec();
}
