#include "diagnostics_list.hpp"

namespace Designer {

SourceDiagnosticsList::SourceDiagnosticsList(QWidget* parent) : QListWidget(parent)
{
    setObjectName(QStringLiteral("sourceDiagnostics"));
    setToolTip(QStringLiteral("Двойной щелчок — перейти к команде ZPL"));
    setWordWrap(true);
    connect(this, &QListWidget::itemActivated, this, [this](const QListWidgetItem* item) {
        if (item && item->data(Qt::UserRole).toInt() >= 0)
            emit sourceActivated(item->data(Qt::UserRole).toInt());
    });
}

void SourceDiagnosticsList::setDiagnostics(const QList<SourceDiagnostic>& diagnostics)
{
    clear();
    for (const auto& diagnostic : diagnostics) {
        auto* item = new QListWidgetItem(diagnostic.text, this);
        item->setData(Qt::UserRole, diagnostic.offset);
        item->setData(Qt::UserRole + 1, diagnostic.detail.code);
        item->setData(Qt::UserRole + 2, static_cast<int>(diagnostic.detail.severity));
        if (diagnostic.detail.labelIndex)
            item->setData(Qt::UserRole + 3, *diagnostic.detail.labelIndex);
        if (diagnostic.detail.fieldId)
            item->setData(Qt::UserRole + 4, *diagnostic.detail.fieldId);
        if (diagnostic.detail.sourceSpan) {
            item->setData(Qt::UserRole + 5, diagnostic.detail.sourceSpan->start);
            item->setData(Qt::UserRole + 6, diagnostic.detail.sourceSpan->length);
        }
        if (diagnostic.offset >= 0)
            item->setToolTip(QStringLiteral("Двойной щелчок — перейти к команде"));
    }
    setVisible(count() > 0);
}

} // namespace Designer
