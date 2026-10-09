#include "gs1_data.hpp"
#include "value_placeholders.hpp"

#include <algorithm>
#include <QDate>
#include <QSet>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
constexpr qsizetype maximumGs1Length = 1500;
constexpr qsizetype maximumGs1Fields = 64;
const QChar groupSeparator{0x1d};

const Gs1Ai* definition(QStringView ai)
{
    for (const auto& entry : gs1ApplicationIdentifiers())
        if (entry.ai == ai) return &entry;
    return nullptr;
}

bool asciiDigit(QChar ch) { return ch >= u'0' && ch <= u'9'; }
bool gs1Character(QChar ch)
{
    // GS1 encodable character set 82 (X), not arbitrary ASCII or Unicode.
    return ch == u'!' || ch == u'"' || (ch >= u'%' && ch <= u'?')
        || (ch >= u'A' && ch <= u'Z') || ch == u'_'
        || (ch >= u'a' && ch <= u'z');
}

bool validCheckDigit(QStringView value)
{
    int sum = 0;
    int weight = 3;
    for (qsizetype i = value.size() - 2; i >= 0; --i) {
        sum += value[i].digitValue() * weight;
        weight = 4 - weight;
    }
    return (10 - sum % 10) % 10 == value.back().digitValue();
}
} // namespace

const QList<Gs1Ai>& gs1ApplicationIdentifiers()
{
    // Bounded subset of https://ref.gs1.org/ai/ (reviewed 2026-10-07).
    // Keep separatorRequired explicit: fixed data length is not sufficient
    // to infer the GS1 pre-defined-length separator rule for arbitrary AIs.
    static const QList<Gs1Ai> entries = [] {
        QList<Gs1Ai> result{
            {u"00"_s, u"SSCC"_s, 18, 18, true, false},
            {u"01"_s, u"GTIN"_s, 14, 14, true, false},
            {u"02"_s, u"GTIN вложения"_s, 14, 14, true, false},
            {u"10"_s, u"Партия"_s, 1, 20, false, true},
            {u"11"_s, u"Дата производства (ГГММДД)"_s, 6, 6, true, false},
            {u"13"_s, u"Дата упаковки (ГГММДД)"_s, 6, 6, true, false},
            {u"15"_s, u"Лучше употребить до (ГГММДД)"_s, 6, 6, true, false},
            {u"16"_s, u"Продать до (ГГММДД)"_s, 6, 6, true, false},
            {u"17"_s, u"Срок годности (ГГММДД)"_s, 6, 6, true, false},
            {u"20"_s, u"Вариант товара"_s, 2, 2, true, false},
            {u"21"_s, u"Серийный номер"_s, 1, 20, false, true},
            {u"22"_s, u"Вариант потребительского товара"_s, 1, 20, false, true},
            {u"30"_s, u"Переменное количество"_s, 1, 8, true, true},
            {u"37"_s, u"Количество вложений"_s, 1, 8, true, true},
            {u"240"_s, u"Дополнительный ID товара"_s, 1, 30, false, true},
            {u"241"_s, u"Номер товара у заказчика"_s, 1, 30, false, true},
            {u"250"_s, u"Вторичный серийный номер"_s, 1, 30, false, true},
            {u"251"_s, u"Ссылка на исходный товар"_s, 1, 30, false, true},
            {u"400"_s, u"Заказ на поставку"_s, 1, 30, false, true},
            {u"420"_s, u"Почтовый индекс доставки"_s, 1, 20, false, true},
            {u"90"_s, u"Данные по соглашению сторон"_s, 1, 30, false, true},
        };
        for (int decimals = 0; decimals <= 5; ++decimals) {
            result.append({u"310%1"_s.arg(decimals), u"Масса, кг (%1 знаков)"_s.arg(decimals), 6, 6, true, false});
            result.append({u"320%1"_s.arg(decimals), u"Масса, фунты (%1 знаков)"_s.arg(decimals), 6, 6, true, false});
        }
        for (int ai = 91; ai <= 99; ++ai)
            result.append({QString::number(ai), u"Внутренние данные компании"_s, 1, 90, false, true});
        return result;
    }();
    return entries;
}

namespace {
std::expected<Gs1Data, QString> buildGs1Impl(const QList<Gs1Field>& fields, bool allowTemplates)
{
    if (fields.isEmpty() || fields.size() > maximumGs1Fields)
        return std::unexpected(u"GS1: укажите от 1 до 64 полей AI."_s);
    Gs1Data result;
    result.fields = fields;
    QSet<QString> present;
    for (qsizetype index = 0; index < fields.size(); ++index) {
        const auto& field = fields[index];
        const auto* ai = definition(field.ai);
        if (!ai) return std::unexpected(u"GS1: AI (%1) пока не поддерживается конструктором."_s.arg(field.ai));
        if (present.contains(field.ai))
            return std::unexpected(u"GS1: AI (%1) указан повторно."_s.arg(field.ai));
        present.insert(field.ai);
        QString literalValue = field.value;
        bool unresolved = false;
        if (allowTemplates) {
            const auto placeholders = valuePlaceholders(field.value);
            if (!placeholders) return std::unexpected(placeholders.error());
            unresolved = !placeholders->isEmpty();
            if (unresolved) {
                literalValue.clear();
                qsizetype offset = 0;
                for (const auto& placeholder : *placeholders) {
                    if (placeholder.kind == ValuePlaceholderKind::GroupSeparator)
                        return std::unexpected(u"GS1: {{GS}} разделяет поля в сыром шаблоне; внутри значения AI разделитель недопустим."_s);
                    literalValue += QStringView(field.value).sliced(offset, placeholder.offset - offset);
                    offset = placeholder.offset + placeholder.length;
                    if (!result.requiredParameters.contains(placeholder.name))
                        result.requiredParameters.append(placeholder.name);
                }
                literalValue += QStringView(field.value).sliced(offset);
            }
        }
        if ((!unresolved && literalValue.size() < ai->minimumLength) || literalValue.size() > ai->maximumLength)
            return std::unexpected(u"GS1: AI (%1) требует %2–%3 символов."_s
                .arg(field.ai).arg(ai->minimumLength).arg(ai->maximumLength));
        if (!std::ranges::all_of(literalValue, ai->numeric ? asciiDigit : gs1Character))
            return std::unexpected(u"GS1: недопустимые символы в AI (%1). %2"_s.arg(field.ai,
                ai->numeric ? u"Разрешены только цифры 0–9."_s
                    : u"Нужен набор GS1 X: латиница, цифры и допустимая пунктуация; без пробелов и кириллицы."_s));
        if (!unresolved && (field.ai == u"00" || field.ai == u"01" || field.ai == u"02") && !validCheckDigit(field.value))
            return std::unexpected(u"GS1: неверная контрольная цифра в AI (%1)."_s.arg(field.ai));
        if (!unresolved && (field.ai == u"11" || field.ai == u"13" || field.ai == u"15" || field.ai == u"16" || field.ai == u"17")) {
            const int year = 2000 + field.value.first(2).toInt();
            const int month = field.value.mid(2, 2).toInt();
            const int day = field.value.last(2).toInt();
            if (!QDate(year, month, std::max(1, day)).isValid())
                return std::unexpected(u"GS1: неверная дата в AI (%1); формат ГГММДД, день 00 означает конец месяца."_s.arg(field.ai));
        }
        result.elementString += field.ai + field.value;
        if (ai->separatorRequired && index + 1 < fields.size()) result.elementString += groupSeparator;
        QString humanValue = field.value;
        humanValue.replace(u"("_s, u"\\("_s);
        humanValue.replace(u")"_s, u"\\)"_s);
        result.canonicalText += u'(' + field.ai + u')' + humanValue;
        if (result.elementString.size() > maximumGs1Length)
            return std::unexpected(u"GS1: данные превышают предел 1500 символов."_s);
    }
    if (!present.contains(u"00"_s) && !present.contains(u"01"_s))
        return std::unexpected(u"GS1: добавьте ключ идентификации (01) GTIN или (00) SSCC."_s);
    if (present.contains(u"01"_s) && (present.contains(u"02"_s) || present.contains(u"37"_s)))
        return std::unexpected(u"GS1: (01) нельзя сочетать с (02) или (37)."_s);
    if ((present.contains(u"02"_s) || present.contains(u"37"_s))
        && !(present.contains(u"00"_s) && present.contains(u"02"_s) && present.contains(u"37"_s)))
        return std::unexpected(u"GS1: вложение требует совместного указания (00), (02) и (37)."_s);
    for (const auto& field : fields) {
        const QString& ai = field.ai;
        const bool needsGtin = ai == u"10" || ai == u"11" || ai == u"13" || ai == u"15"
            || ai == u"16" || ai == u"17" || ai == u"20" || ai == u"30" || ai == u"240"
            || ai == u"241" || ai.startsWith(u"310") || ai.startsWith(u"320");
        if (needsGtin && !(present.contains(u"01"_s) || present.contains(u"02"_s)))
            return std::unexpected(u"GS1: AI (%1) требует GTIN (01) или (02)."_s.arg(ai));
        if ((ai == u"21" || ai == u"22" || ai == u"250" || ai == u"251") && !present.contains(u"01"_s))
            return std::unexpected(u"GS1: AI (%1) требует GTIN (01)."_s.arg(ai));
        if (ai == u"250" && !present.contains(u"21"_s))
            return std::unexpected(u"GS1: AI (250) требует серийный номер (21)."_s);
    }
    for (const auto prefix : {u"310", u"320"})
        if (std::ranges::count_if(fields, [prefix](const Gs1Field& field) { return field.ai.startsWith(prefix); }) > 1)
            return std::unexpected(u"GS1: нельзя указывать несколько AI одного семейства массы."_s);
    return result;
}

std::expected<Gs1Data, QString> parseGs1Impl(QStringView input, bool allowVisibleSeparator, bool allowTemplates)
{
    // Human notation can double every value parenthesis through escaping.
    if (input.size() > maximumGs1Length * 2 + maximumGs1Fields * 8)
        return std::unexpected(u"GS1: слишком длинная строка данных."_s);
    QString text = input.toString();
    if (text.startsWith(u"]d2")) text.remove(0, 3);
    if (allowVisibleSeparator && !text.startsWith(u'(')) text.replace(u"<GS>"_s, QString(groupSeparator));
    if (text.startsWith(groupSeparator)) text.remove(0, 1);
    QList<Gs1Field> fields;
    const bool human = text.startsWith(u'(');
    qsizetype offset = 0;
    while (offset < text.size()) {
        if (fields.size() >= maximumGs1Fields)
            return std::unexpected(u"GS1: допускается не более 64 полей AI."_s);
        QString ai;
        QString value;
        if (human) {
            if (text[offset] != u'(')
                return std::unexpected(u"GS1: ожидается поле вида (01)значение."_s);
            const qsizetype close = text.indexOf(u')', offset + 1);
            if (close < 0 || close - offset < 3 || close - offset > 5)
                return std::unexpected(u"GS1: AI в скобках должен содержать 2–4 цифры."_s);
            ai = text.mid(offset + 1, close - offset - 1);
            offset = close + 1;
            while (offset < text.size() && text[offset] != u'(') {
                const QChar ch = text[offset++];
                if (ch == u'\\') {
                    if (offset >= text.size() || (text[offset] != u'(' && text[offset] != u')'))
                        return std::unexpected(u"GS1: обратная косая черта экранирует только скобки в значении."_s);
                    value += text[offset++];
                } else {
                    if (ch == u')' || ch == groupSeparator)
                        return std::unexpected(u"GS1: не смешивайте скобочную запись и GS; скобки значения экранируйте как \\( и \\)."_s);
                    value += ch;
                }
            }
        } else {
            const Gs1Ai* found = nullptr;
            for (int length = 2; length <= 4 && offset + length <= text.size(); ++length) {
                found = definition(QStringView(text).sliced(offset, length));
                if (found) break;
            }
            if (!found)
                return std::unexpected(u"GS1: неизвестный AI в позиции %1; проверьте разделители GS."_s.arg(offset + 1));
            ai = found->ai;
            offset += ai.size();
            if (found->separatorRequired) {
                const qsizetype next = text.indexOf(groupSeparator, offset);
                value = text.mid(offset, next < 0 ? -1 : next - offset);
                offset = next < 0 ? text.size() : next + 1;
                if (next == text.size() - 1)
                    return std::unexpected(u"GS1: лишний GS в конце данных."_s);
            } else {
                value = text.mid(offset, found->maximumLength);
                offset += value.size();
            }
        }
        fields.append({ai, value});
    }
    return buildGs1Impl(fields, allowTemplates);
}

std::expected<Gs1Data, QString> parseRawGs1Template(QStringView input, bool allowVisibleSeparator)
{
    QString text = input.toString();
    if (text.startsWith(u"]d2")) text.remove(0, 3);
    if (text.startsWith(groupSeparator)) text.remove(0, 1);
    if (allowVisibleSeparator) text.replace(u"<GS>"_s, QString(groupSeparator));
    const auto placeholders = valuePlaceholders(text);
    if (!placeholders) return std::unexpected(placeholders.error());
    const auto tokenAt = [&](qsizetype offset) -> const ValuePlaceholder* {
        for (const auto& token : *placeholders)
            if (token.offset == offset) return &token;
        return nullptr;
    };
    const auto aiAt = [&](qsizetype offset) -> const Gs1Ai* {
        for (int length = 2; length <= 4 && offset + length <= text.size(); ++length)
            if (const auto* ai = definition(QStringView(text).sliced(offset, length))) return ai;
        return nullptr;
    };
    const auto separatorLength = [&](qsizetype offset) -> qsizetype {
        if (offset >= text.size()) return 0;
        if (text[offset] == groupSeparator) return 1;
        if (const auto* token = tokenAt(offset); token && token->kind == ValuePlaceholderKind::GroupSeparator)
            return token->length;
        return 0;
    };
    QList<Gs1Field> fields;
    qsizetype offset = 0;
    while (offset < text.size()) {
        if (fields.size() >= maximumGs1Fields)
            return std::unexpected(u"GS1: допускается не более 64 полей AI."_s);
        const auto* ai = aiAt(offset);
        if (!ai)
            return std::unexpected(u"GS1: неизвестный AI в позиции %1 сырого шаблона."_s.arg(offset + 1));
        offset += ai->ai.size();
        const qsizetype begin = offset;
        if (!ai->separatorRequired) {
            if (const auto* token = tokenAt(offset); token && token->kind != ValuePlaceholderKind::GroupSeparator) {
                // Длина подставляемого значения неизвестна. Одно поле — один
                // полный токен; смесь с литералами требует скобочной записи.
                offset += token->length;
            } else {
                if (offset + ai->maximumLength > text.size())
                    return std::unexpected(u"GS1: неполное фиксированное поле (%1) в сыром шаблоне."_s.arg(ai->ai));
                offset += ai->maximumLength;
            }
            if (separatorLength(offset))
                return std::unexpected(u"GS1: после фиксированного поля (%1) разделитель GS не требуется."_s.arg(ai->ai));
        } else {
            while (offset < text.size() && !separatorLength(offset)) {
                if (const auto* nextAi = aiAt(offset); nextAi && tokenAt(offset + nextAi->ai.size()))
                    return std::unexpected(u"GS1: перед AI (%1) в сыром шаблоне нужен {{GS}}; неоднозначные значения задавайте как (AI)значение."_s.arg(nextAi->ai));
                if (const auto* token = tokenAt(offset)) offset += token->length;
                else ++offset;
            }
        }
        fields.append({ai->ai, text.mid(begin, offset - begin)});
        if (const qsizetype separator = separatorLength(offset)) {
            offset += separator;
            if (offset == text.size()) return std::unexpected(u"GS1: лишний GS в конце сырого шаблона."_s);
        }
    }
    auto result = buildGs1Impl(fields, true);
    if (!result) return result;
    // Сырой шаблон нужен и внешнему formatter: сохраняем написание {{GS}},
    // {{NN}} и [[name]], а не заменяем их скобочным представлением.
    result->canonicalText = input.toString();
    result->elementString = text;
    return result;
}
} // namespace

std::expected<Gs1Data, QString> buildGs1(const QList<Gs1Field>& fields)
{
    return buildGs1Impl(fields, false);
}

std::expected<Gs1Data, QString> parseGs1(QStringView input, bool allowVisibleSeparator)
{
    return parseGs1Impl(input, allowVisibleSeparator, false);
}

std::expected<Gs1Data, QString> buildGs1Template(const QList<Gs1Field>& fields)
{
    return buildGs1Impl(fields, true);
}

std::expected<Gs1Data, QString> parseGs1Template(QStringView input, bool allowVisibleSeparator)
{
    const auto placeholders = valuePlaceholders(input);
    if (!placeholders) return std::unexpected(placeholders.error());
    if (placeholders->isEmpty()) return parseGs1(input, allowVisibleSeparator);
    if (placeholders->size() == 1 && placeholders->first().offset == 0 && placeholders->first().length == input.size()
        && placeholders->first().kind != ValuePlaceholderKind::GroupSeparator) {
        Gs1Data result;
        result.canonicalText = result.elementString = input.toString();
        result.requiredParameters = {placeholders->first().name};
        result.wholeCodePlaceholder = true;
        return result;
    }
    if (!input.startsWith(u'(')) return parseRawGs1Template(input, allowVisibleSeparator);
    return parseGs1Impl(input, allowVisibleSeparator, true);
}

std::expected<Gs1Data, QString> resolveGs1Template(QStringView input, const QMap<QString, QString>& values)
{
    const auto data = parseGs1Template(input);
    if (!data) return std::unexpected(data.error());
    if (data->wholeCodePlaceholder) {
        const auto resolved = resolveValueTemplate(input, values);
        if (!resolved) return std::unexpected(resolved.error());
        return parseGs1(*resolved);
    }
    QList<Gs1Field> fields;
    for (const auto& field : data->fields) {
        const auto resolved = resolveValueTemplate(field.value, values);
        if (!resolved) return std::unexpected(resolved.error());
        fields.append({field.ai, *resolved});
    }
    return buildGs1(fields);
}
} // namespace Designer
