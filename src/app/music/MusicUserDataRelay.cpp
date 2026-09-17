#include "app/music/MusicUserDataRelay.h"

#include <utility>

#include "app/ItemActions.h"
#include "app/controllers/LiveUpdateService.h"
#include "app/music/MusicRepository.h"
#include "app/music/models/MusicModelBase.h"
#include "server/dto/MediaItem.h"

namespace strmqt::music {

MusicUserDataRelay::MusicUserDataRelay(MusicRepository *repository, QObject *parent)
    : QObject(parent)
    , m_repository(repository)
{
}

void MusicUserDataRelay::addModel(MusicModelBase *model)
{
    if (model && !m_models.contains(model))
        m_models.append(model);
}

void MusicUserDataRelay::bind(ItemActions *actions, LiveUpdateService *live)
{
    if (actions) {
        connect(actions, &ItemActions::favoriteChanged, this, &MusicUserDataRelay::onFavouriteChanged);
        connect(actions, &ItemActions::playedChanged, this, &MusicUserDataRelay::onPlayedChanged);
    }
    if (live)
        connect(live, &LiveUpdateService::userDataPatched, this, &MusicUserDataRelay::onUserDataPatched);
}

void MusicUserDataRelay::apply(const QString &itemId, const UserDataPatch &patch)
{
    if (itemId.isEmpty())
        return;
    m_models.removeIf([](const QPointer<MusicModelBase> &model) { return model.isNull(); });
    for (const QPointer<MusicModelBase> &model : std::as_const(m_models))
        model->applyUserData(itemId, patch);
    if (m_repository)
        m_repository->noteUserDataChanged(itemId);
}

void MusicUserDataRelay::onFavouriteChanged(const QString &itemId, bool favourite)
{
    UserDataPatch patch;
    patch.favourite = favourite;
    apply(itemId, patch);
}

void MusicUserDataRelay::onPlayedChanged(const QString &itemId, bool played)
{
    UserDataPatch patch;
    patch.played = played;
    apply(itemId, patch);
}

void MusicUserDataRelay::onUserDataPatched(const QVariantList &entries)
{
    for (const QVariant &value : entries) {
        const QVariantMap entry = value.toMap();
        UserDataPatch patch;
        if (entry.contains(QStringLiteral("favorite")))
            patch.favourite = entry.value(QStringLiteral("favorite")).toBool();
        if (entry.contains(QStringLiteral("played")))
            patch.played = entry.value(QStringLiteral("played")).toBool();
        if (entry.contains(QStringLiteral("playCount")))
            patch.playCount = entry.value(QStringLiteral("playCount")).toInt();
        if (entry.contains(QStringLiteral("positionTicks")))
            patch.positionMs = entry.value(QStringLiteral("positionTicks")).toLongLong() / kTicksPerMs;
        apply(entry.value(QStringLiteral("itemId")).toString(), patch);
    }
}

} // namespace strmqt::music
