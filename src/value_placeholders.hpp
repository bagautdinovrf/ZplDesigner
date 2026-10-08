#pragma once

#include <expected>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace Designer {
// Дополнительные поля Newl2 отличаются от GS1-полей форматом дат и регистром.
enum class ValuePlaceholderKind { Named, NumericField, AdditionalField, GroupSeparator };
struct ValuePlaceholder {
    QString name;
    qsizetype offset = 0;
    qsizetype length = 0;
    ValuePlaceholderKind kind = ValuePlaceholderKind::Named;
};
// {{NN}} — поле данных, {{GS}} — ASCII 29, [[name]] — ключ в нижнем регистре.
// [[11]]/[[17]] имеют собственные ключи для готовых человекочитаемых дат.
// Прежние {{name}} и [[name.with-dots]] остаются доступны для старых шаблонов.
std::expected<QList<ValuePlaceholder>, QString> valuePlaceholders(QStringView text);
std::expected<QStringList, QString> requiredValueParameters(QStringView text);
// Substitution is one pass. Inserted values are never interpreted as templates.
std::expected<QString, QString> resolveValueTemplate(QStringView text, const QMap<QString, QString>& values);
} // namespace Designer
