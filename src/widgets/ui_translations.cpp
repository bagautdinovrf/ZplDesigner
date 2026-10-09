#include "ui_translations.hpp"

#include <QCoreApplication>
#include <QTranslator>

// A direct reference also retains the resource when designer_widgets is static.
static void initializeTranslationResource()
{
    Q_INIT_RESOURCE(designer_translations);
}

namespace Designer {
namespace {
class RussianUiTranslator final : public QTranslator {
public:
    using QTranslator::QTranslator;

    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr, int n = -1) const override
    {
        if (qstrcmp(context, "QPlatformTheme") == 0) {
            if (qstrcmp(sourceText, "OK") == 0) return QStringLiteral("Принять");
            // Qt's generic "Отклонить" is ambiguous in a save confirmation.
            if (qstrcmp(sourceText, "Discard") == 0) return QStringLiteral("Не сохранять");
        }
        return QTranslator::translate(context, sourceText, disambiguation, n);
    }
};
} // namespace

bool installRussianUiTranslations()
{
    auto* application = QCoreApplication::instance();
    if (!application) return false;
    const auto name = QStringLiteral("designerRussianUiTranslator");
    if (application->findChild<QTranslator*>(name, Qt::FindDirectChildrenOnly)) return true;

    initializeTranslationResource();
    auto* translator = new RussianUiTranslator(application);
    translator->setObjectName(name);
    if (!translator->load(QStringLiteral(":/designer/translations/qtbase_ru.qm"))
        || !QCoreApplication::installTranslator(translator)) {
        delete translator;
        return false;
    }
    return true;
}

} // namespace Designer
