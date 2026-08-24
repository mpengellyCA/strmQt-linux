#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace strmqt {

// Stable descriptor surface for Home's vertical ListView. Child media models
// update independently; this model changes only when a rail itself is added,
// removed, moved, or renamed, so one rail's item count cannot reset every
// sibling delegate.
class HomeRailModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    struct Descriptor
    {
        QString key;
        QString title;
        QObject *railModel = nullptr;
        bool library = false;
        bool wide = false;
        QString genreId;

        bool operator==(const Descriptor &) const = default;
    };

    enum Role
    {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        RailModelRole,
        LibraryRole,
        WideRole,
        GenreIdRole,
    };

    explicit HomeRailModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDescriptors(QList<Descriptor> descriptors);

signals:
    void countChanged();

private:
    void updateDescriptor(int row, const Descriptor &descriptor);

    QList<Descriptor> m_descriptors;
};

} // namespace strmqt
