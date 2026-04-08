#ifndef CACHYOS_GUI_LOG_MODEL_HPP
#define CACHYOS_GUI_LOG_MODEL_HPP

#include <QAbstractListModel>
#include <QList>
#include <QString>

// Append-only list model backing the install log view. Holds every line so the whole
// log stays scrollable, while keeping insertion O(1): the previous QML approach rebuilt
// the entire line array on every message (quadratic) and had to cap the history to stay
// usable. A C++ QAbstractListModel appends with a single-row beginInsertRows/endInsertRows
// and only materialises the rows the ListView actually shows.
class LogModel final : public QAbstractListModel {
    Q_OBJECT

 public:
    enum Roles : int {
        LineRole = Qt::UserRole + 1,
    };

    explicit LogModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

 public slots:
    // Appends one line. Must be called on the GUI thread (the model has no locking).
    void appendLine(const QString& line);
    // Drops all lines (e.g. when a fresh install starts).
    void clear();

 private:
    QList<QString> m_lines;
};

#endif  // CACHYOS_GUI_LOG_MODEL_HPP
