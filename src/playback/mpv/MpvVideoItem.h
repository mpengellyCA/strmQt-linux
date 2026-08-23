#pragma once

#include <QQuickFramebufferObject>
#include <QtQmlIntegration/qqmlintegration.h>

namespace strmqt {

class MpvPlayer;

// Video plane: renders the attached MpvPlayer's output into the Qt Quick scene
// via mpv's OpenGL render API (scene graph pinned to GL, PLAN §3.2).
class MpvVideoItem : public QQuickFramebufferObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(MpvVideo)
    Q_PROPERTY(QObject *player READ playerObject WRITE setPlayerObject NOTIFY playerChanged)

public:
    explicit MpvVideoItem(QQuickItem *parent = nullptr);

    Renderer *createRenderer() const override;

    QObject *playerObject() const;
    void setPlayerObject(QObject *player);
    MpvPlayer *player() const { return m_player; }

signals:
    void playerChanged();

private:
    MpvPlayer *m_player = nullptr;
};

} // namespace strmqt
