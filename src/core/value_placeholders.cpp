#include "value_placeholders.hpp"

#include <algorithm>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
constexpr qsizetype maximumTemplateLength = 64 * 1024;
constexpr qsizetype maximumResolvedLength = 8 * 1024 * 1024;

bool nameStart(QChar ch)
{
    return (ch >= u'A' && ch <= u'Z') || (ch >= u'a' && ch <= u'z') || ch == u'_';
}
bool namePart(QChar ch)
{
    return nameStart(ch) || (ch >= u'0' && ch <= u'9') || ch == u'.' || ch == u'-';
}
bool wordPart(QChar ch)
{
    return nameStart(ch) || (ch >= u'0' && ch <= u'9');
}
} // namespace

std::expected<QList<ValuePlaceholder>, QString> valuePlaceholders(QStringView text)
{
    if (text.size() > maximumTemplateLength)
        return std::unexpected(u"Строка шаблона превышает 64 КиБ."_s);
    QList<ValuePlaceholder> result;
    qsizetype offset = 0;
    while (offset < text.size()) {
        const auto tail = text.sliced(offset);
        const bool braces = tail.startsWith(u"{{");
        const bool brackets = tail.startsWith(u"[[");
        if (!braces && !brackets) {
            if (tail.startsWith(u"}}") || tail.startsWith(u"]]"))
                return std::unexpected(u"Лишнее закрытие переменной в позиции %1."_s.arg(offset + 1));
            ++offset;
            continue;
        }
        const auto closing = braces ? QStringView(u"}}") : QStringView(u"]]");
        const qsizetype end = text.indexOf(closing, offset + 2);
        if (end < 0)
            return std::unexpected(u"Переменная в позиции %1 не закрыта."_s.arg(offset + 1));
        const auto name = text.sliced(offset + 2, end - offset - 2);
        const bool numericField = braces && name.size() == 2
            && std::ranges::all_of(name, [](QChar ch) { return ch >= u'0' && ch <= u'9'; });
        const bool groupSeparator = braces && name.compare(u"GS", Qt::CaseInsensitive) == 0;
        const bool named = !name.isEmpty() && nameStart(name.front()) && std::ranges::all_of(name, namePart);
        const bool additional = brackets && !name.isEmpty() && std::ranges::all_of(name, wordPart);
        if (!numericField && !groupSeparator && !named && !additional)
            return std::unexpected(u"Некорректная переменная в позиции %1. Поле данных: {{01}}, разделитель: {{GS}}, дополнительное поле: [[name]] или [[17]]. Для прежних {{name}} имя начинается с латинской буквы или _, далее допустимы также цифры, точка и дефис."_s.arg(offset + 1));
        auto kind = ValuePlaceholderKind::Named;
        QString key = name.toString();
        if (groupSeparator) { kind = ValuePlaceholderKind::GroupSeparator; key = u"GS"_s; }
        else if (numericField) kind = ValuePlaceholderKind::NumericField;
        else if (brackets) {
            kind = ValuePlaceholderKind::AdditionalField;
            key = name.toString().toLower();
            if (key == u"11" || key == u"17") key = u"[["_s + key + u"]]"_s;
        }
        result.append({key, offset, end + 2 - offset, kind});
        offset = end + 2;
    }
    return result;
}

std::expected<QStringList, QString> requiredValueParameters(QStringView text)
{
    const auto placeholders = valuePlaceholders(text);
    if (!placeholders) return std::unexpected(placeholders.error());
    QStringList result;
    for (const auto& placeholder : *placeholders)
        if (placeholder.kind != ValuePlaceholderKind::GroupSeparator && !result.contains(placeholder.name))
            result.append(placeholder.name);
    return result;
}

std::expected<QString, QString> resolveValueTemplate(QStringView text, const QMap<QString, QString>& values)
{
    const auto placeholders = valuePlaceholders(text);
    if (!placeholders) return std::unexpected(placeholders.error());
    QStringList missing;
    for (const auto& placeholder : *placeholders)
        if (placeholder.kind != ValuePlaceholderKind::GroupSeparator
            && !values.contains(placeholder.name) && !missing.contains(placeholder.name)) missing.append(placeholder.name);
    if (!missing.isEmpty())
        return std::unexpected(u"Не заданы значения переменных: %1."_s.arg(missing.join(u", "_s)));
    QString result;
    qsizetype offset = 0;
    for (const auto& placeholder : *placeholders) {
        const QString value = placeholder.kind == ValuePlaceholderKind::GroupSeparator
            ? QString(QChar(0x1d)) : values.value(placeholder.name);
        const qsizetype literalLength = placeholder.offset - offset;
        if (result.size() + literalLength + value.size() > maximumResolvedLength)
            return std::unexpected(u"Результат подстановки превышает 8 МиБ."_s);
        result += text.sliced(offset, literalLength);
        result += value;
        offset = placeholder.offset + placeholder.length;
    }
    if (result.size() + text.size() - offset > maximumResolvedLength)
        return std::unexpected(u"Результат подстановки превышает 8 МиБ."_s);
    result += text.sliced(offset);
    return result;
}
} // namespace Designer
