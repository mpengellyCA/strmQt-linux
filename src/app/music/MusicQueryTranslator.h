#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "server/dto/ItemsQuery.h"
#include "server/dto/music/MusicQuery.h"

// MusicQuery (the UI's words) → ItemsQuery (Emby's), per Crate spec §5.2–5.3 and
// the measured capabilities in server/emby/MusicServerCapabilities.h.
namespace strmqt::music {

struct SortKey
{
    QString key;
    QString label;
    bool defaultDescending = false;
};

struct LetterRange
{
    QString greaterOrEqual;
    QString lessThan;
};

namespace MusicQueryTranslator {

QList<SortKey> sortKeysFor(Section section);
QString sortFields(Section section, const QString &key);
LetterRange letterRange(const QString &letter);
QStringList codecsFor(FormatFilter format);
bool formatFilterable(Section section);
QList<FormatFilter> formatOptions(Section section);
ItemsQuery toItemsQuery(const MusicQuery &query, int startIndex, int limit);

} // namespace MusicQueryTranslator
} // namespace strmqt::music
