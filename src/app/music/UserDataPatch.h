#pragma once

#include <QtGlobal>

#include <optional>

namespace strmqt::music {

// A partial user-data change. Unset fields are left alone: a local favourite
// toggle says nothing about play counts.
struct UserDataPatch
{
    std::optional<bool> favourite;
    std::optional<bool> played;
    std::optional<int> playCount;
    std::optional<qint64> positionMs;
};

} // namespace strmqt::music
