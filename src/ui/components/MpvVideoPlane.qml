import QtQuick
import StrmQt

// Video plane for the libmpv engine. Kept in its own file (rather than an inline
// Component in PlayerPage) so that a build configured without an engine simply
// lacks the file, instead of leaving an unresolvable type reference that would
// make PlayerPage itself fail to load. See VlcVideoPlane.qml.
MpvVideo {
}
