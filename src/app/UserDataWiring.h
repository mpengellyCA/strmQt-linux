#pragma once

namespace strmqt {

class DetailsController;
class ItemActions;
class LiveUpdateService;

// How user data — watched, favourite, resume point — moves between the live
// service, ItemActions and the details controller. Out of Application so a
// test runs the wiring the app runs instead of a copy of it.
//
// ItemActions sees every rich record before anyone else does, as the
// service's stage rather than as one more connection. It decides "did this
// record change the answer" against what the registered models held BEFORE
// anyone patched them, patches those models and the play queue itself, and
// only then emits playedChanged / favoriteChanged. What it hands back is what
// every other binding receives: a field with a request on the wire carries the
// user's intent, not the record's value, so a stale record crossing a toggle
// cannot put the old value back on a search row, an episode or an album heart.
//
// The details page's own item is often in no model ItemActions holds, so it
// is wired both ways: ItemActions' changes reach DetailsController, and the
// details reply reaches ItemActions as a server record. Without the second,
// the hero button's label and the verb it fires fall back to different
// snapshots, and "Mark watched" can mark a watched film unwatched.
void wireUserData(LiveUpdateService *live, ItemActions *actions, DetailsController *details);

} // namespace strmqt
