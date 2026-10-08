#include "data_matrix_values.hpp"
#include "design_document.hpp"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTextStream>

using namespace Designer;
using namespace Qt::StringLiterals;

namespace {
void printJson(const QJsonObject& result)
{
    QTextStream output(stdout);
    output.setEncoding(QStringConverter::Utf8);
    output << QJsonDocument(result).toJson(QJsonDocument::Indented) << Qt::endl;
}

std::expected<QByteArray, QString> readFile(const QString& path, qint64 limit)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::unexpected(file.errorString());
    const auto bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError) return std::unexpected(file.errorString());
    if (bytes.size() > limit) return std::unexpected(u"Input file exceeds its size limit."_s);
    return bytes;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
} // namespace

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(u"designer_inspect_zpl"_s);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Inspect an explicitly supplied local ZPL without modifying it."_s);
    parser.addHelpOption();
    parser.addPositionalArgument(u"file"_s, u"Local UTF-8 ZPL file"_s);
    QCommandLineOption valuesOption(u"values"_s, u"JSON object file containing preview parameter values"_s, u"file"_s);
    QCommandLineOption outputOption(u"output-dir"_s, u"Directory for preview PNGs and a filled ZPL copy"_s, u"directory"_s);
    parser.addOptions({valuesOption, outputOption});
    parser.process(application);
    if (parser.positionalArguments().size() != 1) parser.showHelp(2);
    const auto path = parser.positionalArguments().first();
    QJsonObject result{{u"file"_s, path}};
    const auto fail = [&result](const QString& error) {
        result.insert(u"error"_s, error);
        printJson(result);
        return 1;
    };
    const auto bytes = readFile(path, 8 * 1024 * 1024);
    if (!bytes) return fail(bytes.error());
    QStringDecoder decoder(QStringDecoder::Utf8,
        QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
    const QString source = decoder.decode(*bytes);
    if (decoder.hasError()) return fail(u"Invalid UTF-8 source."_s);
    auto document = fromZpl(source);
    if (!document) return fail(document.error());
    const bool preserved = toZpl(*document) == source;
    result.insert(u"visual"_s, document->visualEditable);
    result.insert(u"sourcePreserved"_s, preserved);
    result.insert(u"elements"_s, static_cast<int>(document->elements.size()));
    result.insert(u"width"_s, document->width);
    result.insert(u"height"_s, document->height);
    result.insert(u"importNotes"_s, QJsonArray::fromStringList(document->importNotes));
    const auto parameters = dataMatrixParameters(*document);
    if (!parameters) return fail(parameters.error());
    result.insert(u"parameters"_s, QJsonArray::fromStringList(*parameters));
    const auto editor = renderEditorPreview(*document);
    if (!editor) return fail(editor.error());
    result.insert(u"editorPreview"_s, true);
    result.insert(u"unresolvedBlocks"_s, static_cast<int>(editor->elementErrors.size()));
    result.insert(u"editorDiagnostics"_s, QJsonArray::fromStringList(editor->diagnostics));
    const auto outputPath = parser.value(outputOption);
    if (!outputPath.isEmpty()) {
        const QFileInfo input(path);
        for (const auto& name : {u"editor-preview.png"_s, u"filled-preview.png"_s, u"filled.zpl"_s}) {
            const QFileInfo output(QDir(outputPath).filePath(name));
            const bool samePath = input.absoluteFilePath().compare(output.absoluteFilePath(), Qt::CaseInsensitive) == 0;
            const bool sameFile = !input.canonicalFilePath().isEmpty()
                && input.canonicalFilePath() == output.canonicalFilePath();
            if (samePath || sameFile) return fail(u"Output directory would overwrite the input file."_s);
        }
        if (!QDir().mkpath(outputPath)) return fail(u"Cannot create output directory."_s);
        const auto imagePath = QDir(outputPath).filePath(u"editor-preview.png"_s);
        if (!editor->image.save(imagePath)) return fail(u"Cannot save editor preview."_s);
        result.insert(u"editorImage"_s, imagePath);
    }
    if (parser.isSet(valuesOption)) {
        const auto valueBytes = readFile(parser.value(valuesOption), 64 * 1024);
        if (!valueBytes) return fail(valueBytes.error());
        QJsonParseError error;
        const auto json = QJsonDocument::fromJson(*valueBytes, &error);
        if (error.error != QJsonParseError::NoError || !json.isObject())
            return fail(u"Values must be a JSON object of string values."_s);
        const auto values = json.object();
        for (auto it = values.begin(); it != values.end(); ++it) {
            if (!it.value().isString()) return fail(u"Every parameter value must be a JSON string."_s);
            document->previewValues.insert(it.key(), it.value().toString());
        }
        const auto resolved = resolveDataMatrixValues(*document);
        if (!resolved) return fail(resolved.error());
        const auto validation = validateForExport(*resolved, false);
        if (!validation) return fail(validation.error());
        const auto filled = toZpl(*resolved, false);
        const auto reopened = fromZpl(filled);
        if (!reopened) return fail(reopened.error());
        if (!reopened->visualEditable || reopened->elements.size() != document->elements.size())
            return fail(u"Filled ZPL did not reopen with all editable blocks."_s);
        const auto remaining = dataMatrixParameters(*reopened);
        if (!remaining || !remaining->isEmpty()) return fail(u"Filled ZPL still contains unresolved parameters."_s);
        const auto preview = renderPreview(*reopened);
        if (!preview) return fail(preview.error());
        result.insert(u"resolvedPreview"_s, true);
        result.insert(u"resolvedDiagnostics"_s, QJsonArray::fromStringList(preview->diagnostics));
        result.insert(u"filledReopens"_s, true);
        if (!outputPath.isEmpty()) {
            const auto imagePath = QDir(outputPath).filePath(u"filled-preview.png"_s);
            const auto filledPath = QDir(outputPath).filePath(u"filled.zpl"_s);
            if (!preview->image.save(imagePath) || !writeFile(filledPath, filled.toUtf8()))
                return fail(u"Cannot save filled preview or ZPL copy."_s);
            result.insert(u"filledImage"_s, imagePath);
            result.insert(u"filledZpl"_s, filledPath);
        }
    }
    printJson(result);
    return preserved && document->visualEditable ? 0 : 1;
}
