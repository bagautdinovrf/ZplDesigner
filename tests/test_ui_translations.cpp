#include "ui_translations.hpp"
#include "print_dialog.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>
#include <QUuid>
#include <QtTest>

using namespace Designer;
using namespace Qt::StringLiterals;

class UiTranslationTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void standardButtonsAreRussian_data();
    void standardButtonsAreRussian();
    void messageDetailsAndInputButtonsAreRussian();
    void staticMessagesAreRussian();
    void printerProfileNameDialogsAreRussian();
};

void UiTranslationTests::initTestCase()
{
    // The Russian application must not depend on the machine's default locale.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QVERIFY(installRussianUiTranslations());
    QVERIFY(QFile::exists(u":/designer/translations/qtbase_ru.qm"_s));
    QVERIFY(installRussianUiTranslations());
    QCOMPARE(qApp->findChildren<QTranslator*>(u"designerRussianUiTranslator"_s,
                                             Qt::FindDirectChildrenOnly).size(), 1);
}

void UiTranslationTests::standardButtonsAreRussian_data()
{
    QTest::addColumn<int>("standardButton");
    QTest::addColumn<QString>("caption");
    QTest::newRow("ok") << int(QMessageBox::Ok) << u"Принять"_s;
    QTest::newRow("save") << int(QMessageBox::Save) << u"Сохранить"_s;
    QTest::newRow("save-all") << int(QMessageBox::SaveAll) << u"Сохранить все"_s;
    QTest::newRow("open") << int(QMessageBox::Open) << u"Открыть"_s;
    QTest::newRow("yes") << int(QMessageBox::Yes) << u"Да"_s;
    QTest::newRow("yes-to-all") << int(QMessageBox::YesToAll) << u"Да для всех"_s;
    QTest::newRow("no") << int(QMessageBox::No) << u"Нет"_s;
    QTest::newRow("no-to-all") << int(QMessageBox::NoToAll) << u"Нет для всех"_s;
    QTest::newRow("abort") << int(QMessageBox::Abort) << u"Прервать"_s;
    QTest::newRow("retry") << int(QMessageBox::Retry) << u"Повторить"_s;
    QTest::newRow("ignore") << int(QMessageBox::Ignore) << u"Пропустить"_s;
    QTest::newRow("close") << int(QMessageBox::Close) << u"Закрыть"_s;
    QTest::newRow("cancel") << int(QMessageBox::Cancel) << u"Отмена"_s;
    QTest::newRow("discard") << int(QMessageBox::Discard) << u"Не сохранять"_s;
    QTest::newRow("help") << int(QMessageBox::Help) << u"Справка"_s;
    QTest::newRow("apply") << int(QMessageBox::Apply) << u"Применить"_s;
    QTest::newRow("reset") << int(QMessageBox::Reset) << u"Сбросить"_s;
    QTest::newRow("restore-defaults") << int(QMessageBox::RestoreDefaults) << u"По умолчанию"_s;
}

void UiTranslationTests::standardButtonsAreRussian()
{
    QFETCH(int, standardButton);
    QFETCH(QString, caption);
    const auto messageButton = static_cast<QMessageBox::StandardButton>(standardButton);
    QMessageBox message(QMessageBox::Information, u"Сообщение"_s, u"Готово"_s, messageButton);
    QVERIFY(message.button(messageButton));
    QCOMPARE(message.button(messageButton)->text().remove(u'&'), caption);

    const auto dialogButton = static_cast<QDialogButtonBox::StandardButton>(standardButton);
    QDialogButtonBox dialogButtons(dialogButton);
    QVERIFY(dialogButtons.button(dialogButton));
    QCOMPARE(dialogButtons.button(dialogButton)->text().remove(u'&'), caption);
}

void UiTranslationTests::messageDetailsAndInputButtonsAreRussian()
{
    QMessageBox message(QMessageBox::Warning, u"Предупреждение"_s, u"Текст"_s, QMessageBox::Ok);
    message.setDetailedText(u"Подробности"_s);
    QAbstractButton* details = nullptr;
    for (auto* button : message.buttons())
        if (message.standardButton(button) == QMessageBox::NoButton) details = button;
    QVERIFY(details);
    QCOMPARE(details->text(), u"Показать подробности..."_s);
    details->click();
    QCOMPARE(details->text(), u"Скрыть подробности..."_s);

    QInputDialog input;
    QCOMPARE(input.okButtonText(), u"Принять"_s);
    QCOMPARE(input.cancelButtonText(), u"Отмена"_s);
}

void UiTranslationTests::staticMessagesAreRussian()
{
    QStringList captions;
    QTimer dismiss;
    dismiss.setInterval(0);
    connect(&dismiss, &QTimer::timeout, this, [&] {
        auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!message) return;
        dismiss.stop();
        for (auto* button : message->buttons()) captions.append(button->text().remove(u'&'));
        const auto accept = message->standardButtons().testFlag(QMessageBox::Save)
            ? QMessageBox::Save : QMessageBox::Ok;
        if (auto* button = message->button(accept)) button->click();
        else message->reject();
    });

    dismiss.start();
    QCOMPARE(QMessageBox::information(nullptr, u"Готово"_s, u"Этикетка напечатана"_s), QMessageBox::Ok);
    QCOMPARE(captions, QStringList{u"Принять"_s});
    captions.clear();
    dismiss.start();
    QCOMPARE(QMessageBox::question(nullptr, u"Сохранить изменения?"_s, u"Этикетка изменена"_s,
                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                  QMessageBox::Save), QMessageBox::Save);
    captions.sort();
    QStringList expected{u"Сохранить"_s, u"Не сохранять"_s, u"Отмена"_s};
    expected.sort();
    QCOMPARE(captions, expected);
}

void UiTranslationTests::printerProfileNameDialogsAreRussian()
{
    const auto previousName = QCoreApplication::applicationName();
    const bool previousTestMode = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(u"profile-translation-%1"_s.arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const auto path = printerProfilesPath();
    const auto restore = qScopeGuard([&] {
        QFile::remove(path);
        QDir().rmdir(QFileInfo(path).absolutePath());
        QCoreApplication::setApplicationName(previousName);
        QStandardPaths::setTestModeEnabled(previousTestMode);
    });

    PrintDialog dialog(sampleDocument());
    auto* destination = dialog.findChild<QComboBox*>(u"printDestination"_s);
    auto* host = dialog.findChild<QLineEdit*>(u"printHost"_s);
    auto* profiles = dialog.findChild<QComboBox*>(u"printProfiles"_s);
    auto* save = dialog.findChild<QPushButton*>(u"printSaveProfile"_s);
    auto* rename = dialog.findChild<QPushButton*>(u"printRenameProfile"_s);
    QVERIFY(destination && host && profiles && save && rename);
    destination->setCurrentIndex(destination->findData(int(PrintConnection::Tcp)));
    host->setText(u"192.0.2.5"_s);

    QStringList captions;
    QString initialName;
    const auto editName = [&](QPushButton* action, const QString& name, bool accept) {
        captions.clear();
        initialName.clear();
        QTimer::singleShot(0, &dialog, [&, name, accept] {
            auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (!input) return;
            captions = {input->okButtonText(), input->cancelButtonText()};
            initialName = input->textValue();
            input->setTextValue(name);
            auto* buttons = input->findChild<QDialogButtonBox*>();
            if (buttons) buttons->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel)->click();
            else input->reject();
        });
        action->click();
    };

    editName(save, u"Отменённый профиль"_s, false);
    QCOMPARE(captions, (QStringList{u"Сохранить"_s, u"Отмена"_s}));
    QVERIFY(!QFile::exists(path));
    editName(save, u"  Склад  "_s, true);
    QCOMPARE(captions, (QStringList{u"Сохранить"_s, u"Отмена"_s}));
    auto stored = loadPrinterProfiles();
    QVERIFY(stored && stored->size() == 1);
    QCOMPARE(stored->front().name, u"Склад"_s);
    QCOMPARE(profiles->currentText(), u"Склад"_s);

    editName(rename, u"  Новый склад  "_s, true);
    QCOMPARE(captions, (QStringList{u"Переименовать"_s, u"Отмена"_s}));
    QCOMPARE(initialName, u"Склад"_s);
    stored = loadPrinterProfiles();
    QVERIFY(stored && stored->size() == 1);
    QCOMPARE(stored->front().name, u"Новый склад"_s);
    editName(rename, u"Отменённое имя"_s, false);
    QCOMPARE(captions, (QStringList{u"Переименовать"_s, u"Отмена"_s}));
    stored = loadPrinterProfiles();
    QVERIFY(stored && stored->size() == 1);
    QCOMPARE(stored->front().name, u"Новый склад"_s);
}

QTEST_MAIN(UiTranslationTests)
#include "test_ui_translations.moc"
