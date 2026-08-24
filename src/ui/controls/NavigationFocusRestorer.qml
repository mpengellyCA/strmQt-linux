import QtQuick

// Constant-space focus restoration for a virtual view. History retains only a
// stable row identity and the old cursor. A short timer lets a controller's
// already-started refill land naturally; this helper never starts pagination
// or any other network work. If the exact row does not arrive, focus settles on
// the nearest eligible row and the pending target is retired.
QtObject {
    id: restorer

    property var model: null
    property int count: 0
    property int currentIndex: -1
    property int settleInterval: 400

    readonly property int maximumIndex: 2147483646
    readonly property int qmlScanLimit: 512
    readonly property bool pending: restorer._pendingIndex >= 0

    property string _pendingIdentity: ""
    property int _pendingIndex: -1
    property int _generation: 0

    signal focusRequested(int index)

    property Timer settleTimer: Timer {
        interval: restorer.settleInterval
        repeat: false
        onTriggered: restorer.finishWithFallback(restorer._generation)
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
        restorer.focusRequested(-1)
        if (!restorer.retry())
            restorer.settleTimer.restart()
        return true
    }

    function cancel(): void {
        ++restorer._generation
        restorer.settleTimer.stop()
        restorer._pendingIdentity = ""
        restorer._pendingIndex = -1
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
        if (!restorer.pending)
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
        restorer.focusRequested(target)
    }
}
