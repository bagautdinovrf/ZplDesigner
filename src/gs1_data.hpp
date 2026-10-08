#pragma once

#include <expected>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace Designer {
struct Gs1Field {
    QString ai;
    QString value;
    bool operator==(const Gs1Field&) const = default;
};
struct Gs1Ai {
    QString ai;
    QString title;
    int minimumLength;
    int maximumLength;
    bool numeric;
    bool separatorRequired;
};
struct Gs1Data {
    QList<Gs1Field> fields;
    // Parenthesized notation; literal value parentheses use \( and \).
    QString canonicalText;
    // Contains ASCII GS separators, but no leading FNC1 or scanner identifier.
    QString elementString;
    QStringList requiredParameters;
    bool wholeCodePlaceholder = false;
};
const QList<Gs1Ai>& gs1ApplicationIdentifiers();
std::expected<Gs1Data, QString> buildGs1(const QList<Gs1Field>& fields);
// Supports (AI)value notation or raw data with GS / <GS>, optionally prefixed ]d2.
// Raw variable-length fields must have a separator before the next AI.
// Disable visible separators when parsing actual printer/scanner bytes so a
// literal <GS> within a value can never be interpreted as a control character.
std::expected<Gs1Data, QString> parseGs1(QStringView text, bool allowVisibleSeparator = true);
// Template variants validate known literals and AI relationships, retaining
// placeholders verbatim. Full value validation follows parameter resolution.
std::expected<Gs1Data, QString> buildGs1Template(const QList<Gs1Field>& fields);
std::expected<Gs1Data, QString> parseGs1Template(QStringView text, bool allowVisibleSeparator = true);
// Resolve fields independently, preventing parentheses in values from becoming
// new AIs; validate the resulting concrete GS1 payload before rendering it.
std::expected<Gs1Data, QString> resolveGs1Template(QStringView text, const QMap<QString, QString>& values);
} // namespace Designer
