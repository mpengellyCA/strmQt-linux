#pragma once

#include "UserProfile.h"

#include <QString>

namespace strmqt {

// Result of a successful authentication against a media server.
// The access token is held in memory here; at-rest persistence goes through
// platform/SecretsStore only.
struct SessionInfo
{
    QString accessToken;
    QString serverId;
    UserProfile user;

    bool isValid() const { return !accessToken.isEmpty() && !user.id.isEmpty(); }
};

} // namespace strmqt
