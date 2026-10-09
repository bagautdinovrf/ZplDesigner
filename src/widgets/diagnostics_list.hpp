#pragma once

#include "preview_pages.hpp"

#include <QListWidget>

namespace Designer {

class SourceDiagnosticsList final : public QListWidget {
    Q_OBJECT
public:
    explicit SourceDiagnosticsList(QWidget* parent = nullptr);
    void setDiagnostics(const QList<SourceDiagnostic>& diagnostics);

signals:
    void sourceActivated(int offset);
};

} // namespace Designer
