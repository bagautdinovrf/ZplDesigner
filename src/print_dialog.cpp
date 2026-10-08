#include "print_dialog.hpp"
#include "label_printing.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPrinterInfo>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QThread>
#include <QVBoxLayout>
#include <QtZpl/qtzpl.hpp>

#include <algorithm>
#include <atomic>
#include <memory>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
QString settingsPath()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return directory.isEmpty() ? QString{} : QDir(directory).filePath(u"printing.ini"_s);
}

void savePreferences(const QString& path, const PrintDestination& destination, LabelPrintMode mode)
{
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) return;
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(u"connection"_s, static_cast<int>(destination.connection));
    settings.setValue(u"mode"_s, static_cast<int>(mode));
    settings.setValue(u"usePrinterPaper"_s, destination.usePrinterPaper);
    settings.setValue(u"printer"_s, destination.printerName);
    settings.setValue(u"host"_s, destination.host);
    settings.setValue(u"port"_s, destination.port);
    settings.sync();
}
} // namespace

PrintDialog::PrintDialog(const DesignDocument& document, QWidget* parent)
    : QDialog(parent), document_(document)
{
    setObjectName(u"printDialog"_s);
    setWindowTitle(u"Печать этикетки"_s);
    setModal(true);
    setMinimumWidth(480);
    resize(620, 440);
    const auto parsed = QtZpl::parse(toZpl(document_));
    if (parsed && !parsed->labels().isEmpty()) {
        rawLabelsPerRepeat_ = 0;
        for (const auto& label : parsed->labels()) {
            int quantity = 1;
            for (const auto& command : label.commands())
                if (const auto* print = std::get_if<QtZpl::PrintQuantity>(&command.payload)) quantity = print->quantity;
            rawLabelsPerRepeat_ += std::max(1, quantity);
        }
    }

    auto* outer = new QVBoxLayout(this);
    dimensions_ = new QLabel(this);
    dimensions_->setObjectName(u"printDimensions"_s);
    dimensions_->setWordWrap(true);
    outer->addWidget(dimensions_);
    controls_ = new QWidget(this);
    auto* controlsLayout = new QVBoxLayout(controls_);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    auto* profileRow = new QHBoxLayout;
    profiles_ = new QComboBox(controls_);
    profiles_->setObjectName(u"printProfiles"_s);
    profiles_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    profiles_->setMinimumContentsLength(14);
    profileRow->addWidget(new QLabel(u"Профиль"_s, controls_));
    profileRow->addWidget(profiles_, 1);
    auto* saveProfileButton = new QPushButton(u"Сохранить…"_s, controls_);
    saveProfileButton->setObjectName(u"printSaveProfile"_s);
    saveProfileButton->setToolTip(u"Сохранить текущие настройки под новым или существующим именем"_s);
    auto* renameProfileButton = new QPushButton(u"Имя…"_s, controls_);
    renameProfileButton->setObjectName(u"printRenameProfile"_s);
    auto* deleteProfileButton = new QPushButton(u"Удалить"_s, controls_);
    deleteProfileButton->setObjectName(u"printDeleteProfile"_s);
    for (auto* button : {saveProfileButton, renameProfileButton, deleteProfileButton}) { button->setAutoDefault(false); profileRow->addWidget(button); }
    controlsLayout->addLayout(profileRow);
    form_ = new QFormLayout;
    destination_ = new QComboBox(this);
    destination_->setObjectName(u"printDestination"_s);
    destination_->addItem(u"Установленный принтер"_s, static_cast<int>(PrintConnection::InstalledPrinter));
    destination_->addItem(u"Прямое подключение по IP"_s, static_cast<int>(PrintConnection::Tcp));
    form_->addRow(u"Подключение"_s, destination_);
    printer_ = new QComboBox(this);
    printer_->setObjectName(u"printPrinter"_s);
    availablePrinterNames_ = QPrinterInfo::availablePrinterNames();
    printer_->addItems(availablePrinterNames_);
    const auto defaultIndex = printer_->findText(QPrinterInfo::defaultPrinterName());
    if (defaultIndex >= 0) printer_->setCurrentIndex(defaultIndex);
    form_->addRow(u"Принтер"_s, printer_);
    host_ = new QLineEdit(this);
    host_->setObjectName(u"printHost"_s);
    host_->setPlaceholderText(u"192.168.1.100 или имя принтера"_s);
    host_->setMaxLength(253);
    form_->addRow(u"IP / имя узла"_s, host_);
    port_ = new QSpinBox(this);
    port_->setObjectName(u"printPort"_s);
    port_->setRange(1, 65535);
    port_->setValue(9100);
    form_->addRow(u"Порт"_s, port_);
    mode_ = new QComboBox(this);
    mode_->setObjectName(u"printMode"_s);
    mode_->addItem(u"PNG"_s, static_cast<int>(LabelPrintMode::Image));
    mode_->addItem(u"ZPL"_s, static_cast<int>(LabelPrintMode::Zpl));
    form_->addRow(u"Печатать как"_s, mode_);
    paper_ = new QComboBox(this);
    paper_->setObjectName(u"printPaper"_s);
    paper_->addItem(u"Бумага принтера, масштаб 100%"_s, true);
    paper_->addItem(u"Бумага размером с этикетку"_s, false);
    form_->addRow(u"Бумага"_s, paper_);
    dpi_ = new QComboBox(this);
    dpi_->setObjectName(u"printDpi"_s);
    dpi_->addItem(u"Как в документе (%1 dpi)"_s.arg(document.dpi), 0);
    for (const int dpi : {203, 300, 600}) dpi_->addItem(u"%1 dpi"_s.arg(dpi), dpi);
    form_->addRow(u"Разрешение принтера"_s, dpi_);
    repeats_ = new QSpinBox(this);
    repeats_->setObjectName(u"printRepeats"_s);
    repeats_->setRange(1, 100);
    form_->addRow(u"Повторов задания"_s, repeats_);
    controlsLayout->addLayout(form_);
    auto* parametersButton = new QPushButton(u"Параметры принтера…"_s, controls_);
    parametersButton->setObjectName(u"printParameters"_s);
    parametersButton->setAutoDefault(false);
    controlsLayout->addWidget(parametersButton);
    parametersSummary_ = new QLabel(controls_);
    parametersSummary_->setObjectName(u"printParametersSummary"_s);
    parametersSummary_->setWordWrap(true);
    parametersSummary_->setTextFormat(Qt::PlainText);
    controlsLayout->addWidget(parametersSummary_);
    outer->addWidget(controls_);
    explanation_ = new QLabel(this);
    explanation_->setObjectName(u"printExplanation"_s);
    explanation_->setTextFormat(Qt::PlainText);
    explanation_->setWordWrap(true);
    outer->addWidget(explanation_);
    status_ = new QLabel(this);
    status_->setObjectName(u"printStatus"_s);
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outer->addWidget(status_);
    outer->addStretch();
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Close, this);
    print_ = buttons_->addButton(u"Печатать"_s, QDialogButtonBox::ActionRole);
    print_->setObjectName(u"printSubmit"_s);
    print_->setDefault(true);
    outer->addWidget(buttons_);
    connect(buttons_, &QDialogButtonBox::rejected, this, &PrintDialog::reject);
    connect(print_, &QPushButton::clicked, this, &PrintDialog::submit);
    connect(destination_, &QComboBox::currentIndexChanged, this, &PrintDialog::refreshOptions);
    connect(mode_, &QComboBox::currentIndexChanged, this, &PrintDialog::refreshOptions);
    connect(paper_, &QComboBox::currentIndexChanged, this, &PrintDialog::refreshOptions);
    connect(printer_, &QComboBox::currentIndexChanged, this, &PrintDialog::refreshOptions);
    connect(host_, &QLineEdit::textChanged, this, &PrintDialog::refreshOptions);
    connect(repeats_, &QSpinBox::valueChanged, this, &PrintDialog::refreshOptions);
    connect(dpi_, &QComboBox::currentIndexChanged, this, &PrintDialog::refreshOptions);
    connect(profiles_, &QComboBox::currentIndexChanged, this, &PrintDialog::selectProfile);
    connect(saveProfileButton, &QPushButton::clicked, this, &PrintDialog::saveProfile);
    connect(renameProfileButton, &QPushButton::clicked, this, &PrintDialog::renameProfile);
    connect(deleteProfileButton, &QPushButton::clicked, this, &PrintDialog::deleteProfile);
    connect(parametersButton, &QPushButton::clicked, this, &PrintDialog::editPrinterParameters);
    loadPreferences();
    loadProfiles();
    refreshOptions();
}

void PrintDialog::reject()
{
    QDialog::reject();
}

void PrintDialog::refreshOptions()
{
    const int dpi = dpi_->currentData().toInt() == 0 ? document_.dpi : dpi_->currentData().toInt();
    dimensions_->setText(u"Этикетка: %1 × %2 dots · печать %3 dpi · %4 × %5 мм. Геометрия документа не меняется."_s
        .arg(document_.width).arg(document_.height).arg(dpi)
        .arg(document_.width * 25.4 / std::max(1, dpi), 0, 'f', 2)
        .arg(document_.height * 25.4 / std::max(1, dpi), 0, 'f', 2));
    const bool tcp = destination_->currentData().toInt() == static_cast<int>(PrintConnection::Tcp);
    if (mode_->count() != (tcp ? 3 : 2)) {
        const auto selectedMode = mode_->currentData();
        const QSignalBlocker blocker(mode_);
        mode_->clear();
        if (tcp) {
            mode_->addItem(u"ZPL"_s, static_cast<int>(LabelPrintMode::Zpl));
            mode_->addItem(u"PNG"_s, static_cast<int>(LabelPrintMode::Png));
            mode_->addItem(u"Преобразование в ZPL"_s, static_cast<int>(LabelPrintMode::Image));
        } else {
            mode_->addItem(u"PNG"_s, static_cast<int>(LabelPrintMode::Image));
            mode_->addItem(u"ZPL"_s, static_cast<int>(LabelPrintMode::Zpl));
        }
        const int selectedIndex = mode_->findData(selectedMode);
        mode_->setCurrentIndex(selectedIndex >= 0 ? selectedIndex
            : mode_->findData(static_cast<int>(LabelPrintMode::Image)));
    }
    const bool raw = mode_->currentData().toInt() == static_cast<int>(LabelPrintMode::Zpl);
    const bool png = mode_->currentData().toInt() == static_cast<int>(LabelPrintMode::Png);
    form_->setRowVisible(printer_, !tcp);
    form_->setRowVisible(host_, tcp);
    form_->setRowVisible(port_, tcp);
    form_->setRowVisible(paper_, !tcp && !raw);
    findChild<QPushButton*>(u"printParameters"_s)->setEnabled(!png);
    parametersSummary_->setText(png
        ? u"Параметры ZPL из профиля не применяются к PNG. Настройки печати задаются на принимающем устройстве."_s
        : !tcp && !raw
            ? u"PNG печатается через драйвер Windows с его настройками. Параметры ZPL из профиля не применяются."_s
        : parameters_.isEmpty()
            ? u"Параметры: принтер / исходный ZPL. Изменённые настройки сохраняются только кнопкой «Сохранить…»."_s
            : u"Заданы параметры принтера. Они применяются к заданию ZPL и могут остаться активными в принтере. "
              u"Смещения корректируют печать, не расположение объектов в редакторе."_s);
    QString explanation;
    if (raw) {
        explanation = u"Принтер должен поддерживать ZPL. Исходное количество печати сохраняется: "
            u"%1 этикеток за один проход, всего %2 с учётом повторов."_s
            .arg(rawLabelsPerRepeat_).arg(rawLabelsPerRepeat_ * repeats_->value());
    } else if (png) {
        explanation = u"QtZpl создаёт PNG этикетки и отправляет его напрямую на указанный IP и порт. "
            u"Принтер или сервер печати должен принимать PNG по этому подключению. "
            u"Каждый повтор передаётся отдельным PNG. Всего этикеток: %1."_s.arg(repeats_->value());
    } else {
        explanation = tcp
            ? u"QtZpl строит чёрно-белое изображение и преобразует его в графику ZPL (^GFA). "
              u"Принтер по этому адресу должен поддерживать ZPL. Размеры в точках сохраняются."_s
            : paper_->currentData().toBool()
                ? u"PNG-изображение этикетки печатается через драйвер Windows на настроенной бумаге принтера, "
                  u"от верхнего левого края области печати. Масштаб — 100%, физический размер этикетки сохраняется."_s
                : u"PNG-изображение этикетки печатается через драйвер Windows. Размер бумаги равен этикетке, "
                  u"масштаб — 100%. Драйвер принтера должен поддерживать такой формат."_s;
        explanation += u" Всего этикеток: %1."_s.arg(repeats_->value());
    }
#ifndef Q_OS_WIN
    if (raw && !tcp) explanation += u" Отправка ZPL в установленный принтер доступна только в Windows; выберите IP."_s;
#endif
    explanation_->setText(explanation);
    bool usable = tcp ? !host_->text().trimmed().isEmpty() : availablePrinterNames_.contains(printer_->currentText());
#ifndef Q_OS_WIN
    usable = usable && (!raw || tcp);
#endif
    print_->setEnabled(usable && !busy_);
}

void PrintDialog::loadPreferences()
{
    const auto path = settingsPath();
    if (path.isEmpty()) return;
    const QSettings settings(path, QSettings::IniFormat);
    const auto select = [](QComboBox* combo, const QVariant& value) {
        const int index = combo->findData(value);
        if (index >= 0) combo->setCurrentIndex(index);
    };
    select(destination_, settings.value(u"connection"_s, static_cast<int>(PrintConnection::InstalledPrinter)));
    select(mode_, settings.value(u"mode"_s, static_cast<int>(LabelPrintMode::Image)));
    select(paper_, settings.value(u"usePrinterPaper"_s, true));
    const int printerIndex = printer_->findText(settings.value(u"printer"_s).toString());
    if (printerIndex >= 0) printer_->setCurrentIndex(printerIndex);
    host_->setText(settings.value(u"host"_s).toString());
    port_->setValue(settings.value(u"port"_s, 9100).toInt());
    // Repeats always start at one to prevent accidental repeat runs.
}

PrinterProfile PrintDialog::currentProfile() const
{
    PrinterProfile profile;
    profile.name = profiles_->currentData().toString();
    profile.destination.connection = static_cast<PrintConnection>(destination_->currentData().toInt());
    profile.destination.printerName = printer_->currentText();
    profile.destination.host = host_->text().trimmed();
    profile.destination.port = static_cast<quint16>(port_->value());
    profile.destination.timeoutMs = timeoutMs_;
    profile.destination.usePrinterPaper = paper_->currentData().toBool();
    profile.mode = static_cast<LabelPrintMode>(mode_->currentData().toInt());
    profile.dpi = dpi_->currentData().toInt();
    profile.parameters = parameters_;
    return profile;
}

void PrintDialog::loadProfiles()
{
    const auto loaded = loadPrinterProfiles();
    catalogueWritable_ = loaded.has_value();
    if (loaded) catalogue_ = *loaded;
    else status_->setText(loaded.error() + u" Файл оставлен без изменений; сохранение профилей отключено."_s);
    refreshProfiles();
}

void PrintDialog::refreshProfiles(const QString& selectName)
{
    const QSignalBlocker blocker(profiles_);
    profiles_->clear();
    profiles_->addItem(u"Без профиля"_s, QString{});
    for (const auto& profile : catalogue_) profiles_->addItem(profile.name, profile.name);
    const int index = profiles_->findData(selectName);
    profiles_->setCurrentIndex(std::max(0, index));
    findChild<QPushButton*>(u"printSaveProfile"_s)->setEnabled(catalogueWritable_);
    findChild<QPushButton*>(u"printRenameProfile"_s)->setEnabled(catalogueWritable_ && index > 0);
    findChild<QPushButton*>(u"printDeleteProfile"_s)->setEnabled(catalogueWritable_ && index > 0);
}

void PrintDialog::selectProfile()
{
    const auto name = profiles_->currentData().toString();
    findChild<QPushButton*>(u"printRenameProfile"_s)->setEnabled(catalogueWritable_ && !name.isEmpty());
    findChild<QPushButton*>(u"printDeleteProfile"_s)->setEnabled(catalogueWritable_ && !name.isEmpty());
    const auto it = std::ranges::find(catalogue_, name, &PrinterProfile::name);
    if (it == catalogue_.end()) {
        parameters_ = {};
        timeoutMs_ = 10000;
        dpi_->setCurrentIndex(0);
        refreshOptions();
        return;
    }
    const auto select = [](QComboBox* combo, const QVariant& value) { combo->setCurrentIndex(combo->findData(value)); };
    select(destination_, static_cast<int>(it->destination.connection));
    select(mode_, static_cast<int>(it->mode));
    select(paper_, it->destination.usePrinterPaper);
    select(dpi_, it->dpi);
    int printerIndex = printer_->findText(it->destination.printerName);
    if (printerIndex < 0 && !it->destination.printerName.isEmpty()) {
        printer_->addItem(it->destination.printerName); // display unavailable devices without substituting a different printer
        printerIndex = printer_->count() - 1;
    }
    printer_->setCurrentIndex(printerIndex);
    host_->setText(it->destination.host);
    port_->setValue(it->destination.port);
    timeoutMs_ = it->destination.timeoutMs;
    parameters_ = it->parameters;
    repeats_->setValue(1);
    refreshOptions();
}

void PrintDialog::saveProfile()
{
    if (!catalogueWritable_) return;
    bool accepted = false;
    auto profile = currentProfile();
    profile.name = QInputDialog::getText(this, u"Сохранить профиль"_s,
        u"Имя профиля (существующее имя заменит его настройки)"_s, QLineEdit::Normal, profile.name, &accepted).trimmed();
    if (!accepted) return;
    auto updated = catalogue_;
    const auto it = std::ranges::find_if(updated, [&](const auto& entry) { return entry.name.compare(profile.name, Qt::CaseInsensitive) == 0; });
    if (it == updated.end()) updated.append(profile);
    else *it = profile;
    const auto saved = savePrinterProfiles(updated);
    if (!saved) { QMessageBox::warning(this, u"Профили принтеров"_s, saved.error()); return; }
    catalogue_ = std::move(updated);
    refreshProfiles(profile.name);
    status_->setText(u"Профиль «%1» сохранён локально."_s.arg(profile.name));
}

void PrintDialog::renameProfile()
{
    if (!catalogueWritable_ || profiles_->currentIndex() <= 0) return;
    bool accepted = false;
    const auto oldName = profiles_->currentData().toString();
    const auto name = QInputDialog::getText(this, u"Имя профиля"_s, u"Новое имя"_s, QLineEdit::Normal, oldName, &accepted).trimmed();
    if (!accepted) return;
    auto updated = catalogue_;
    const auto it = std::ranges::find(updated, oldName, &PrinterProfile::name);
    if (it == updated.end()) return;
    it->name = name;
    const auto saved = savePrinterProfiles(updated);
    if (!saved) { QMessageBox::warning(this, u"Профили принтеров"_s, saved.error()); return; }
    catalogue_ = std::move(updated);
    refreshProfiles(name);
    status_->setText(u"Профиль переименован."_s);
}

void PrintDialog::deleteProfile()
{
    if (!catalogueWritable_ || profiles_->currentIndex() <= 0) return;
    const auto name = profiles_->currentData().toString();
    auto updated = catalogue_;
    updated.removeIf([&](const auto& entry) { return entry.name == name; });
    const auto saved = savePrinterProfiles(updated);
    if (!saved) { QMessageBox::warning(this, u"Профили принтеров"_s, saved.error()); return; }
    catalogue_ = std::move(updated);
    refreshProfiles();
    // Keep the current job settings, but do not leave a phantom selected profile.
    status_->setText(u"Профиль «%1» удалён. Текущие настройки задания сохранены в окне."_s.arg(name));
}

void PrintDialog::editPrinterParameters()
{
    QDialog dialog(this);
    dialog.setObjectName(u"printerParametersDialog"_s);
    dialog.setWindowTitle(u"Параметры принтера ZPL"_s);
    auto* layout = new QVBoxLayout(&dialog);
    auto* note = new QLabel(u"«Принтер / исходный ZPL» оставляет параметр без изменения. "
        u"Заданные значения применяются только к ZPL (включая изображение по IP); "
        u"поддерживаемые значения зависят от модели принтера. Настройки могут сохраняться в устройстве после задания."_s, &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* form = new QFormLayout;
    const auto spin = [&](const QString& title, const QString& name, std::optional<int> value, int minimum, int maximum) {
        auto* editor = new QSpinBox(&dialog);
        editor->setObjectName(name);
        editor->setRange(minimum - 1, maximum);
        editor->setSpecialValueText(u"Принтер / исходный ZPL"_s);
        editor->setValue(value.value_or(minimum - 1));
        form->addRow(title, editor);
        return editor;
    };
    const auto combo = [&](const QString& title, const QString& name, const QString& value,
                           std::initializer_list<std::pair<QString, QString>> choices) {
        auto* editor = new QComboBox(&dialog);
        editor->setObjectName(name);
        editor->addItem(u"Принтер / исходный ZPL"_s, QString{});
        for (const auto& [text, code] : choices) editor->addItem(text, code);
        editor->setCurrentIndex(std::max(0, editor->findData(value)));
        form->addRow(title, editor);
        return editor;
    };
    auto* darkness = spin(u"Насыщенность, 0–30"_s, u"printerDarkness"_s, parameters_.darkness, 0, 30);
    auto* speed = spin(u"Скорость, дюймов/с"_s, u"printerSpeed"_s, parameters_.speed, 1, 14);
    auto* media = combo(u"Тип носителя"_s, u"printerMedia"_s, parameters_.mediaTracking,
        {{u"Непрерывный"_s, u"N"_s}, {u"Зазор / просечка"_s, u"Y"_s}, {u"Чёрная метка"_s, u"M"_s},
         {u"Зазор, web sensor"_s, u"W"_s}, {u"Автоопределение"_s, u"A"_s}});
    auto* method = combo(u"Метод печати"_s, u"printerMethod"_s, parameters_.printMethod,
        {{u"Прямая термопечать"_s, u"D"_s}, {u"Термотрансфер"_s, u"T"_s}});
    auto* mode = combo(u"Выдача этикетки"_s, u"printerOutput"_s, parameters_.printMode,
        {{u"Отрыв"_s, u"T"_s}, {u"Отделитель"_s, u"P"_s}, {u"Намотка"_s, u"R"_s},
         {u"Отрезчик"_s, u"C"_s}, {u"Аппликатор"_s, u"A"_s}});
    auto* tear = spin(u"Положение отрыва, dots"_s, u"printerTearOff"_s, parameters_.tearOff, -120, 120);
    auto* top = spin(u"Вертикальное смещение, dots"_s, u"printerLabelTop"_s, parameters_.labelTop, -120, 120);
    auto* shift = spin(u"Горизонтальное смещение, dots"_s, u"printerLabelShift"_s, parameters_.labelShift, -9999, 9999);
    shift->setToolTip(u"Положительное значение сдвигает печать влево, отрицательное — вправо."_s);
    auto* length = spin(u"Макс. длина носителя, dots"_s, u"printerMaximumLength"_s, parameters_.maximumLength, 1, 32000);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* reset = buttons->addButton(u"Все по умолчанию"_s, QDialogButtonBox::ResetRole);
    connect(reset, &QPushButton::clicked, &dialog, [=] {
        for (auto* editor : {darkness, speed, tear, top, shift, length}) editor->setValue(editor->minimum());
        for (auto* editor : {media, method, mode}) editor->setCurrentIndex(0);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto value = [](QSpinBox* editor) -> std::optional<int> {
        return editor->value() == editor->minimum() ? std::nullopt : std::optional{editor->value()};
    };
    parameters_ = {value(darkness), value(speed), media->currentData().toString(), method->currentData().toString(),
        mode->currentData().toString(), value(tear), value(top), value(shift), value(length)};
    refreshOptions();
}

void PrintDialog::submit()
{
    if (busy_) return;
    const auto profile = currentProfile();
    const auto destination = profile.destination;
    const auto mode = profile.mode;
    const int repeats = repeats_->value();
    busy_ = true;
    controls_->setEnabled(false);
    buttons_->button(QDialogButtonBox::Close)->setText(u"Закрыть окно — отправка продолжится"_s);
    refreshOptions();
    status_->setText(u"Подготовка и отправка задания… Можно закрыть это окно: отправка продолжится, "
        u"результат появится отдельным сообщением."_s);
    using Result = std::expected<PrintSubmission, QString>;
    const auto result = std::make_shared<Result>(std::unexpected(u"Отправка не завершена."_s));
    const auto quitting = std::make_shared<std::atomic<bool>>(false);
    const auto preferencePath = settingsPath();
    const QPointer<PrintDialog> dialog(this);
    const QPointer<QWidget> messageParent(parentWidget());
    // Native spooler calls can block. The UI and TCP timeout remain independent
    // of those driver calls, and no widgets are accessed from this thread.
    auto* worker = QThread::create([snapshot = document_, profile, mode, repeats, destination, result] {
        const auto prepared = prepareLabelPrint(snapshot, mode, repeats);
        if (QThread::currentThread()->isInterruptionRequested()) {
            *result = std::unexpected(u"Приложение закрывается; задание не отправлено."_s);
            return;
        }
        if (!prepared) { *result = std::unexpected(prepared.error()); return; }
        const auto configured = configureLabelPrint(*prepared, profile.dpi, profile.parameters);
        *result = configured ? submitLabelPrint(*configured, destination) : Result(std::unexpected(configured.error()));
    });
    connect(worker, &QThread::finished, qApp, [dialog, messageParent, result, quitting, preferencePath, destination, mode] {
        if (quitting->load()) return;
        if (*result) savePreferences(preferencePath, destination, mode);
        if (dialog && dialog->isVisible()) {
            dialog->busy_ = false;
            dialog->controls_->setEnabled(true);
            dialog->buttons_->button(QDialogButtonBox::Close)->setText(u"Закрыть"_s);
            dialog->status_->setText(*result ? result->value().message : result->error());
            dialog->refreshOptions();
        } else if (*result) QMessageBox::information(messageParent, u"Печать этикетки"_s, result->value().message);
        else QMessageBox::warning(messageParent, u"Печать этикетки"_s, result->error());
    });
    connect(qApp, &QCoreApplication::aboutToQuit, worker, [worker, quitting] {
        quitting->store(true);
        worker->requestInterruption();
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
} // namespace Designer
