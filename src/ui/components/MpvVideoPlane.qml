import QtQuick
import StrmQt

// Video plane for the libmpv engine. Kept in its own file (rather than an inline
// Component in PlayerPage) so that a build configured without an engine simply
// lacks the file, instead of leaving an unresolvable type reference that would
// make PlayerPage itself fail to load. See VlcVideoPlane.qml.
MpvVideo {
    id: plane

    // Upscaling inputs (spec 2026-10-03 §4.3–4.4). Forwarded, never
    // interpreted: PlayerController decides. Held in a property so the
    // handlers below call through a qualified name.
    readonly property var controller: PlayerCtl

    function forwardSize(): void {
        plane.controller.setVideoOutputSize(plane.outputPixelSize.width,
                                            plane.outputPixelSize.height);
    }

    onOutputPixelSizeChanged: plane.forwardSize()
    onGpuRendererChanged: plane.controller.setGpuRenderer(plane.gpuRenderer)
    onGpuTimingSampled: (p95Ms, meanMs, samples) => plane.controller.reportGpuTiming(p95Ms, meanMs, samples)
    Component.onCompleted: plane.forwardSize()
}
