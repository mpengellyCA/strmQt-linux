#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>

#include "app/music/UserDataPatch.h"

namespace strmqt {
class ItemActions;
class LiveUpdateService;
}

namespace strmqt::music {

class MusicModelBase;
class MusicRepository;

// The single user-data sink for music (Crate spec §3.4): a heart tap, a played
// toggle or a server push patches every live music model in place and drops
// the repository caches that hold the item. Pages never refetch for this.
class MusicUserDataRelay : public QObject
{
    Q_OBJECT

public:
    explicit MusicUserDataRelay(MusicRepository *repository, QObject *parent = nullptr);

    void addModel(MusicModelBase *model);
    void bind(ItemActions *actions, LiveUpdateService *live);
    void apply(const QString &itemId, const UserDataPatch &patch);

public slots:
    void onFavouriteChanged(const QString &itemId, bool favourite);
    void onPlayedChanged(const QString &itemId, bool played);
    void onUserDataPatched(const QVariantList &entries);

private:
    MusicRepository *m_repository;
    QList<QPointer<MusicModelBase>> m_models;
};

} // namespace strmqt::music
