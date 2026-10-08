#include "ui_translations.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QInputDialog>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QTranslator>
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
    QTest::newRow("ok") << int(QMessageBox::Ok) << u"ОК"_s;
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
    QCOMPARE(input.okButtonText(), u"ОК"_s);
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
    QCOMPARE(captions, QStringList{u"ОК"_s});
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

QTEST_MAIN(UiTranslationTests)
#include "test_ui_translations.moc"
