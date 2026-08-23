import QtQuick
import StrmQt

// Video plane for the libVLC escape-hatch engine. This file is added to the QML
// module only when STRMQT_WITH_VLC found libvlc; without it the VlcVideo type is
// never registered, and PlayerPage must therefore not reference it directly.
VlcVideo {
}
