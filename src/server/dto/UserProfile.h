#pragma once

#include <QString>

namespace strmqt {

// Backend-neutral user identity. Mapped from Emby's User object (M1).
struct UserProfile
{
    QString id;
    QString name;
};

} // namespace strmqt
