#include "HomeRailModel.h"

namespace strmqt {

HomeRailModel::HomeRailModel(QObject *parent) : QAbstractListModel(parent) {}

int HomeRailModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_descriptors.size());
}

QVariant HomeRailModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_descriptors.size())
        return {};

    const Descriptor &descriptor = m_descriptors.at(index.row());
    switch (role) {
    case KeyRole:
        return descriptor.key;
    case TitleRole:
        return descriptor.title;
    case RailModelRole:
        return QVariant::fromValue(descriptor.railModel);
    case LibraryRole:
        return descriptor.library;
    case WideRole:
        return descriptor.wide;
    case GenreIdRole:
        return descriptor.genreId;
    default:
        return {};
    }
}

QHash<int, QByteArray> HomeRailModel::roleNames() const
{
    return {
        {KeyRole, "railKey"},       {TitleRole, "title"}, {RailModelRole, "railModel"},
        {LibraryRole, "library"},   {WideRole, "wide"},   {GenreIdRole, "genreId"},
    };
}

void HomeRailModel::setDescriptors(QList<Descriptor> descriptors)
{
    const int oldCount = static_cast<int>(m_descriptors.size());

    // Match by stable key. Moving a surviving descriptor preserves its delegate;
    // inserting/removing affects only the structural rail that actually changed.
    for (int target = 0; target < descriptors.size(); ++target) {
        const Descriptor &wanted = descriptors.at(target);
        if (target < m_descriptors.size() && m_descriptors.at(target).key == wanted.key) {
            updateDescriptor(target, wanted);
            continue;
        }

        int existing = -1;
        for (int row = target + 1; row < m_descriptors.size(); ++row) {
            if (m_descriptors.at(row).key == wanted.key) {
                existing = row;
                break;
            }
        }

        if (existing >= 0) {
            beginMoveRows({}, existing, existing, {}, target);
            m_descriptors.move(existing, target);
            endMoveRows();
            updateDescriptor(target, wanted);
            continue;
        }

        beginInsertRows({}, target, target);
        m_descriptors.insert(target, wanted);
        endInsertRows();
    }

    if (m_descriptors.size() > descriptors.size()) {
        const int first = descriptors.size();
        const int last = m_descriptors.size() - 1;
        beginRemoveRows({}, first, last);
        m_descriptors.remove(first, last - first + 1);
        endRemoveRows();
    }

    if (oldCount != static_cast<int>(m_descriptors.size()))
        emit countChanged();
}

void HomeRailModel::updateDescriptor(int row, const Descriptor &descriptor)
{
    Descriptor &current = m_descriptors[row];
    QList<int> changedRoles;
    if (current.title != descriptor.title)
        changedRoles.append(TitleRole);
    if (current.railModel != descriptor.railModel)
        changedRoles.append(RailModelRole);
    if (current.library != descriptor.library)
        changedRoles.append(LibraryRole);
    if (current.wide != descriptor.wide)
        changedRoles.append(WideRole);
    if (current.genreId != descriptor.genreId)
        changedRoles.append(GenreIdRole);

    if (changedRoles.isEmpty())
        return;
    current = descriptor;
    const QModelIndex changed = index(row);
    emit dataChanged(changed, changed, changedRoles);
}

} // namespace strmqt
