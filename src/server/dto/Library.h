#pragma once

#include <QString>

namespace strmqt {

// A top-level library view ("Movies", "TV Shows", ...). Mapped from Emby's
// CollectionFolder items in /Users/{uid}/Views.
struct Library
{
    QString id;
    QString name;
    // Normalized collection kind: "movies", "tvshows", "music", ... Empty when the
    // server does not classify the folder.
    QString collectionType;
    QString primaryImageTag;
};

} // namespace strmqt
