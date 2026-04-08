#include "LogModel.hpp"

LogModel::LogModel(QObject* parent)
  : QAbstractListModel(parent) {
}

int LogModel::rowCount(const QModelIndex& parent) const {
    // Flat list: only the invalid (root) parent has rows.
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_lines.size());
}

QVariant LogModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_lines.size()) {
        return {};
    }
    if (role == LineRole || role == Qt::DisplayRole) {
        return m_lines.at(index.row());
    }
    return {};
}

QHash<int, QByteArray> LogModel::roleNames() const {
    return {{LineRole, QByteArrayLiteral("line")}};
}

void LogModel::appendLine(const QString& line) {
    const int row = static_cast<int>(m_lines.size());
    beginInsertRows(QModelIndex(), row, row);
    m_lines.push_back(line);
    endInsertRows();
}

void LogModel::clear() {
    if (m_lines.isEmpty()) {
        return;
    }
    beginResetModel();
    m_lines.clear();
    endResetModel();
}
