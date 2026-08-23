#pragma once

#include "MediaItem.h"

#include <QList>

namespace strmqt {

// One page of a paged item query (PLAN §3.3 PagedList<MediaItem>).
struct ItemsPage
{
    QList<MediaItem> items;
    int totalRecordCount = 0;
    int startIndex = 0;
};

} // namespace strmqt
