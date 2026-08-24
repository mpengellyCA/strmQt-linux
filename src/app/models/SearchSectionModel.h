#pragma once

#include "app/models/MediaItemModel.h"

#include <QSortFilterProxyModel>
#include <QVariantMap>

namespace strmqt {

// A live, ordered view of one Search result kind. SearchController keeps one
// MediaItemModel as the result owner; these proxies give QML labelled sections
// without copying rows or reimplementing user-state synchronization in JS.
class SearchSectionModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum class Section
    {
        Movies,
        Series,
        Episodes,
        Collections,
        Artists,
        Albums,
        Tracks,
        Other,
    };
    Q_ENUM(Section)

    enum ExtraRole
    {
        SourceRowRole = MediaItemModel::SubtitleRole + 1,
        ArtistTextRole,
        AlbumTextRole,
    };
    Q_ENUM(ExtraRole)

    SearchSectionModel(Section section, MediaItemModel *source, QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int sourceRow(int row) const;
    Q_INVOKABLE int indexOfNavigationIdentity(const QString &identity) const;

    Section section() const { return m_section; }

signals:
    void countChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    static bool isKnownType(const QString &type);
    static bool typeBelongsToSection(const QString &type, Section section);

    Section m_section;
    MediaItemModel *m_source;
};

} // namespace strmqt
