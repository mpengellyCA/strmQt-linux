#pragma once

#include <QObject>

namespace strmqt {

// Abstract seam for media server backends (Emby first; Jellyfin/Plex/UPnP later).
// The full async surface — authenticate, libraries, items, playbackTicket, images,
// progress reporting, websocket events — is specified in ARCHITECTURE.md and lands with M1.
// M0 fixes only the seam so dependency direction is established from day one.
class MediaServerBackend : public QObject
{
    Q_OBJECT

public:
    explicit MediaServerBackend(QObject *parent = nullptr);
    ~MediaServerBackend() override;

    virtual QString backendName() const = 0;
};

} // namespace strmqt
