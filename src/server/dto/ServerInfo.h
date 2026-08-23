#pragma once

#include <QString>

namespace strmqt {

// Public (unauthenticated) server identity, e.g. Emby /System/Info/Public.
struct ServerInfo
{
    QString id;
    QString name;
    QString version;
};

} // namespace strmqt
