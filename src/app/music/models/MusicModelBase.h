#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>

#include "app/music/UserDataPatch.h"

namespace strmqt::music {

// Contract shared by every music model (Crate spec §3.4): count, paging,
// get(row) with MediaItemModel's keys, and the navigation identity lookup
// NavigationFocusRestorer uses ("i:<id>" or "p:<playlistItemId>").
class MusicModelBase : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int totalRecordCount READ totalRecordCount NOTIFY totalRecordCountChanged)
    Q_PROPERTY(bool canLoadMore READ canLoadMore NOTIFY totalRecordCountChanged)

public:
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : size();
    }
    int count() const { return size(); }
    int totalRecordCount() const { return m_total; }
    bool canLoadMore() const { return size() < m_total; }

    Q_INVOKABLE virtual QVariantMap get(int row) const = 0;
    Q_INVOKABLE int indexOfNavigationIdentity(const QString &identity) const;
    Q_INVOKABLE QString idAt(int row) const;

    virtual void applyUserData(const QString &itemId, const UserDataPatch &patch) = 0;

signals:
    void countChanged();
    void totalRecordCountChanged();

protected:
    virtual int size() const = 0;
    virtual QString idOf(int row) const = 0;
    virtual QString playlistItemIdOf(int row) const
    {
        Q_UNUSED(row);
        return {};
    }

    void rebuildIndex();
    void extendIndex(int fromRow);
    void setTotal(int total);
    QList<int> rowsFor(const QString &itemId) const { return m_rowsById.value(itemId); }

private:
    void indexRow(int row);

    QHash<QString, int> m_rowByIdentity;
    QHash<QString, QList<int>> m_rowsById;
    int m_total = 0;
};

} // namespace strmqt::music
