#pragma once

#include "app/music/models/MusicModelBase.h"

namespace strmqt::music {

// Storage and paging for a model of one DTO type with an `id` member. Not a
// QObject class of its own (templates cannot carry Q_OBJECT); concrete models
// derive from it and add Q_OBJECT, roles and applyUserData.
template<class T>
class MusicListModel : public MusicModelBase
{
public:
    using MusicModelBase::MusicModelBase;

    void setItems(QList<T> items, int total = -1)
    {
        beginResetModel();
        m_items = std::move(items);
        onItemsReset();
        endResetModel();
        rebuildIndex();
        setTotal(total < 0 ? size() : total);
        emit countChanged();
    }

    void appendItems(const QList<T> &items, int total = -1)
    {
        if (!items.isEmpty()) {
            const int from = size();
            beginInsertRows(QModelIndex(), from, from + static_cast<int>(items.size()) - 1);
            m_items.append(items);
            onItemsAppended(from);
            endInsertRows();
            extendIndex(from);
            emit countChanged();
        }
        setTotal(total < 0 ? totalRecordCount() : total);
    }

    void clear() { setItems({}, 0); }
    const QList<T> &items() const { return m_items; }
    const T &at(int row) const { return m_items.at(row); }

protected:
    int size() const override { return static_cast<int>(m_items.size()); }
    QString idOf(int row) const override { return m_items.at(row).id; }
    virtual void onItemsReset() {}
    virtual void onItemsAppended(int fromRow) { Q_UNUSED(fromRow); }
    void notifyRow(int row, const QList<int> &roles) { emit dataChanged(index(row), index(row), roles); }

    QList<T> m_items;
};

} // namespace strmqt::music
