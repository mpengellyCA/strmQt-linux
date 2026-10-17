#include "UserDataWiring.h"

#include "ItemActions.h"
#include "controllers/DetailsController.h"
#include "controllers/LiveUpdateService.h"

#include <QPointer>

namespace strmqt {

void wireUserData(LiveUpdateService *live, ItemActions *actions, DetailsController *details)
{
    if (live && actions) {
        live->setUserDataStage(
            [guard = QPointer<ItemActions>(actions)](const QVariantList &entries) {
                return guard ? guard->applyUserDataPatches(entries) : entries;
            });
    }
    if (actions && details) {
        QObject::connect(actions, &ItemActions::playedChanged, details,
                         &DetailsController::notePlayed);
        QObject::connect(actions, &ItemActions::favoriteChanged, details,
                         &DetailsController::noteFavorite);
        QObject::connect(details, &DetailsController::itemUserDataLoaded, actions,
                         [actions](const QString &itemId, bool played, bool favorite,
                                   qint64 positionTicks) {
                             actions->applyExternalUserData(itemId, played, favorite,
                                                            positionTicks);
                         });
    }
}

} // namespace strmqt
