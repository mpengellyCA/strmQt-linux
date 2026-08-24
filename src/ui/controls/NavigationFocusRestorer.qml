import QtQuick

// Constant-space focus restoration for a virtual view. History retains only a
// stable row identity and the old cursor. A controller tells the helper while
// its already-started refill is active; this helper never starts pagination or
// any other network work. Once that refill reaches a terminal state, a short
// settle interval lets the model notification land before focus falls back to
// the nearest eligible row.
QtObject {
    id: restorer

    property var model: null
    property int count: 0
    property int currentIndex: -1
    property bool refillActive: false
    property int settleInterval: 400
    // Longer than EmbyClient's per-request transfer deadline. This is an
    // inactivity guard, not a total refill deadline: every model advance
    // restarts it, so a bounded multi-page playlist walk may take as long as its
    // pages require without leaving a genuinely wedged loading signal live
    // forever.
    property int stallInterval: 20000

    readonly property int maximumIndex: 2147483646
    readonly property int qmlScanLimit: 512
    readonly property bool pending: restorer._pendingIndex >= 0

    property string _pendingIdentity: ""
    property int _pendingIndex: -1
    property int _generation: 0

    signal focusRequested(int index)
    signal fallbackRequested()

    property Timer settleTimer: Timer {
        interval: restorer.settleInterval
        repeat: false
        onTriggered: restorer.finishWithFallback(restorer._generation)
    }

    // Controller requests are bounded independently, but keep one final
    // inactivity guard here as well. It is restarted by noteProgress(), never
    // merely by rediscovering the same locator.
    property Timer stallTimer: Timer {
        interval: restorer.stallInterval
        repeat: false
        onTriggered: restorer.finishWithFallback(restorer._generation)
    }

    onRefillActiveChanged: {
        if (!restorer.pending)
            return
        if (restorer.refillActive) {
            restorer.settleTimer.stop()
            restorer.stallTimer.restart()
            return
        }
        restorer.stallTimer.stop()
        if (!restorer.retry())
            restorer.settleTimer.restart()
    }

    onCountChanged: restorer.noteProgress()

    property Connections modelProgress: Connections {
        target: Qt.isQtObject(restorer.model) ? restorer.model : null
        ignoreUnknownSignals: true
        function onModelReset() { Qt.callLater(restorer.noteProgress) }
    }

    function boundedId(value): string {
        const text = value === undefined || value === null ? "" : String(value)
        return text.length <= 1024 ? text : text.slice(0, 1024)
    }

    function rowAt(index): var {
        if (!restorer.model || index < 0 || index >= restorer.count)
            return null
        if (typeof restorer.model.get === "function")
            return restorer.model.get(index)
        if (typeof restorer.model.itemAt === "function")
            return restorer.model.itemAt(index)
        if (restorer.model[index] !== undefined)
            return restorer.model[index]
        return null
    }

    function identityForRow(row): string {
        if (!row)
            return ""
        if (row.playlistItemId !== undefined && row.playlistItemId !== null
                && String(row.playlistItemId).length > 0)
            return restorer.boundedId("p:" + String(row.playlistItemId))
        if (row.itemId !== undefined && row.itemId !== null
                && String(row.itemId).length > 0)
            return restorer.boundedId("i:" + String(row.itemId))
        if (row.libraryId !== undefined && row.libraryId !== null
                && String(row.libraryId).length > 0)
            return restorer.boundedId("i:" + String(row.libraryId))
        if (row.id !== undefined && row.id !== null && String(row.id).length > 0)
            return restorer.boundedId("i:" + String(row.id))
        return ""
    }

    function identityAt(index): string {
        return restorer.identityForRow(restorer.rowAt(index))
    }

    function snapshot(): var {
        // A pending restore describes the page's previous visit, not a new user
        // choice on this visit. Saving it again can resurrect a target after its
        // owner has already timed out or been destroyed.
        if (restorer.pending)
            return { "valid": false, "identity": "", "index": -1 }
        const index = Number(restorer.currentIndex)
        if (!Number.isInteger(index) || index < 0 || index > restorer.maximumIndex)
            return { "valid": false, "identity": "", "index": -1 }
        return { "valid": true, "identity": restorer.identityAt(index), "index": index }
    }

    function restore(identity, index): bool {
        const numericIndex = Number(index)
        if (!Number.isInteger(numericIndex) || numericIndex < 0
                || numericIndex > restorer.maximumIndex)
            return false

        const boundedIdentity = restorer.boundedId(identity)
        // Loader retries may rediscover the same owner. They must not replenish
        // its settling interval or create a second unit of work.
        if (restorer.pending && restorer._pendingIdentity === boundedIdentity
                && restorer._pendingIndex === numericIndex)
            return true

        restorer.cancel()
        restorer._pendingIdentity = boundedIdentity
        restorer._pendingIndex = numericIndex
        ++restorer._generation
        if (restorer.refillActive)
            restorer.stallTimer.restart()
        else if (!restorer.retry())
            restorer.settleTimer.restart()
        return true
    }

    function cancel(): void {
        ++restorer._generation
        restorer.settleTimer.stop()
        restorer.stallTimer.stop()
        restorer._pendingIdentity = ""
        restorer._pendingIndex = -1
    }

    function noteProgress(): bool {
        if (!restorer.pending)
            return false
        // Rows visible during an active replacement may belong to the previous
        // controller snapshot. Do not certify their identity until the owner
        // reports a coherent terminal state.
        if (restorer.refillActive) {
            restorer.stallTimer.restart()
            return false
        }
        return restorer.retry()
    }

    function indexedTarget(): int {
        if (!restorer.model || restorer._pendingIdentity.length === 0)
            return -1
        if (typeof restorer.model.indexOfNavigationIdentity === "function")
            return Number(restorer.model.indexOfNavigationIdentity(restorer._pendingIdentity))

        // Test/local QML ListModels have no maintained C++ identity index. Keep
        // their compatibility scan deliberately small and bounded.
        const countToScan = Math.min(restorer.count, restorer.qmlScanLimit)
        for (let i = 0; i < countToScan; ++i) {
            if (restorer.identityAt(i) === restorer._pendingIdentity)
                return i
        }
        return -1
    }

    function retry(): bool {
        if (!restorer.pending || restorer.refillActive)
            return false

        let target = -1
        if (restorer._pendingIndex < restorer.count) {
            const expectedIdentity = restorer.identityAt(restorer._pendingIndex)
            if (restorer._pendingIdentity.length === 0
                    || expectedIdentity === restorer._pendingIdentity)
                target = restorer._pendingIndex
        }
        if (target < 0)
            target = restorer.indexedTarget()

        if (target < 0 || target >= restorer.count)
            return false

        restorer.cancel()
        restorer.focusRequested(target)
        return true
    }

    function finishWithFallback(generation): void {
        if (!restorer.pending || Number(generation) !== restorer._generation)
            return
        if (restorer.retry())
            return
        const target = restorer.count > 0
                     ? Math.min(restorer._pendingIndex, restorer.count - 1) : -1
        restorer.cancel()
        if (target >= 0)
            restorer.focusRequested(target)
        else
            restorer.fallbackRequested()
    }
}
