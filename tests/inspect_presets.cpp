#include "design_document.hpp"
#include "preset_catalog.hpp"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace {
void printJson(const QJsonObject& result)
{
    QTextStream output(stdout);
    output.setEncoding(QStringConverter::Utf8);
    output << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
}
} // namespace

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("designer_inspect_presets"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Inspect a local QSettings Presets INI with QtZpl Designer."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("catalog"), QStringLiteral("INI file containing the Presets array"));
    QCommandLineOption outputOption(QStringLiteral("output-dir"),
        QStringLiteral("Optional directory for rendered PNGs"), QStringLiteral("directory"));
    parser.addOption(outputOption);
    parser.process(app);
    if (parser.positionalArguments().size() != 1)
        parser.showHelp(2);
    const QString path = parser.positionalArguments().first();
    const auto presets = Designer::readPresetCatalog(path);
    if (!presets) {
        printJson({{QStringLiteral("catalog"), path}, {QStringLiteral("error"), presets.error()}});
        return 1;
    }
    const QString outputPath = parser.value(outputOption);
    if (!outputPath.isEmpty() && !QDir().mkpath(outputPath)) {
        printJson({{QStringLiteral("error"), QStringLiteral("Cannot create output directory: %1").arg(outputPath)}});
        return 1;
    }

    QJsonArray results;
    int validCount = 0, visualCount = 0, preservedCount = 0;
    bool success = true;
    for (qsizetype index = 0; index < presets->size(); ++index) {
        const auto& preset = presets->at(index);
        QJsonObject result{
            {QStringLiteral("index"), static_cast<int>(index)},
            {QStringLiteral("name"), preset.name},
            {QStringLiteral("valid"), false},
            {QStringLiteral("visual"), false},
            {QStringLiteral("preserved"), false},
        };
        const auto parsed = Designer::fromZpl(preset.source);
        if (!parsed) {
            result.insert(QStringLiteral("error"), parsed.error());
            success = false;
        } else {
            const bool preserved = Designer::toZpl(*parsed) == preset.source;
            result.insert(QStringLiteral("preserved"), preserved);
            result.insert(QStringLiteral("visual"), parsed->visualEditable);
            preservedCount += preserved ? 1 : 0;
            visualCount += parsed->visualEditable ? 1 : 0;
            success = success && preserved;
            const auto preview = Designer::renderPreview(*parsed);
            if (!preview) {
                result.insert(QStringLiteral("error"), preview.error());
                result.insert(QStringLiteral("diagnostics"), QJsonArray::fromStringList(parsed->importNotes));
                success = false;
            } else {
                result.insert(QStringLiteral("valid"), true);
                result.insert(QStringLiteral("width"), preview->image.width());
                result.insert(QStringLiteral("height"), preview->image.height());
                result.insert(QStringLiteral("diagnostics"), QJsonArray::fromStringList(preview->diagnostics));
                ++validCount;
                if (!outputPath.isEmpty()) {
                    const QString filename = QStringLiteral("preset-%1.png").arg(index + 1, 2, 10, QLatin1Char('0'));
                    const QString imagePath = QDir(outputPath).filePath(filename);
                    if (preview->image.save(imagePath))
                        result.insert(QStringLiteral("image"), imagePath);
                    else {
                        result.insert(QStringLiteral("error"), QStringLiteral("Failed to write PNG: %1").arg(imagePath));
                        success = false;
                    }
                }
            }
        }
        results.append(result);
    }
    printJson({
        {QStringLiteral("catalog"), path},
        {QStringLiteral("count"), static_cast<int>(presets->size())},
        {QStringLiteral("valid"), validCount},
        {QStringLiteral("visual"), visualCount},
        {QStringLiteral("preserved"), preservedCount},
        {QStringLiteral("results"), results},
    });
    return success ? 0 : 1;
}
