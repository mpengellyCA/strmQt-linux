#pragma once

#include "server/dto/Library.h"

#include <QAbstractListModel>
#include <QList>

namespace strmqt {

// List model over the user's library views ("Movies", "TV Shows", ...).
class LibraryListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        CollectionTypeRole,
        ImageUrlRole,
    };

    explicit LibraryListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOfNavigationIdentity(const QString &identity) const;

    void setLibraries(QList<Library> libraries);
    const QList<Library> &libraries() const { return m_libraries; }

signals:
    void countChanged();

private:
    QList<Library> m_libraries;
};

} // namespace strmqt
