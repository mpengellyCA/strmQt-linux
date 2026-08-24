import QtQuick

// Constant-space focus restoration for virtual views. Navigation history keeps
// only snapshot()'s item id/index; this live helper waits for an asynchronous
// refill and asks the owning page for only a bounded number of additional
// pages. It never retains a model row or a delegate object.
QtObject {
    id: restorer

    property var model: null
    property int count: 0
    property int currentIndex: -1
    property bool canRequestPage: false

    // Library/Music paging is 100 rows per request. The measured product-scale
    // ceiling is the 56,283-row Songs library, so 60,000 is a bounded restore
    // surface with headroom; 600 advancing requests can reach every supported
    // cursor. An index outside this declared contract is rejected immediately
    // instead of starting an open-ended network walk.
    readonly property int maximumIndex: 59999
    readonly property int scanLimit: 10000
    readonly property int pageRequestLimit: 600
    readonly property bool pending: restorer._pendingIndex >= 0

    property string _pendingItemId: ""
    property int _pendingIndex: -1
    property int _lastRequestedCount: -1
    property int _pageRequests: 0

    signal focusRequested(int index)
    signal pageRequested()

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

    function itemIdAt(index): string {
        const row = restorer.rowAt(index)
        if (!row)
            return ""
        if (row.itemId !== undefined && row.itemId !== null)
            return restorer.boundedId(row.itemId)
        if (row.id !== undefined && row.id !== null)
            return restorer.boundedId(row.id)
        return ""
    }

    function snapshot(): var {
        if (restorer.pending) {
            return { "valid": true, "itemId": restorer._pendingItemId,
                     "index": restorer._pendingIndex }
        }
        const index = Number(restorer.currentIndex)
        if (!Number.isInteger(index) || index < 0 || index > restorer.maximumIndex)
            return { "valid": false, "itemId": "", "index": -1 }
        return { "valid": true, "itemId": restorer.itemIdAt(index), "index": index }
    }

    function restore(itemId, index): bool {
        const numericIndex = Number(index)
        if (!Number.isInteger(numericIndex) || numericIndex < 0
                || numericIndex > restorer.maximumIndex)
            return false
        restorer._pendingItemId = restorer.boundedId(itemId)
        restorer._pendingIndex = numericIndex
        restorer._lastRequestedCount = -1
        restorer._pageRequests = 0
        // Give the virtual view the tab stop immediately. retry() will move its
        // cursor only when the exact row becomes eligible.
        restorer.focusRequested(-1)
        restorer.retry()
        return true
    }

    function cancel(): void {
        restorer._pendingItemId = ""
        restorer._pendingIndex = -1
        restorer._lastRequestedCount = -1
        restorer._pageRequests = 0
    }

    function retry(): bool {
        if (!restorer.pending)
            return false

        let target = -1
        if (restorer._pendingIndex < restorer.count) {
            const expectedId = restorer.itemIdAt(restorer._pendingIndex)
            if (restorer._pendingItemId.length === 0
                    || expectedId === restorer._pendingItemId)
                target = restorer._pendingIndex
        }

        if (target < 0 && restorer._pendingItemId.length > 0) {
            const countToScan = Math.min(restorer.count, restorer.scanLimit)
            for (let i = 0; i < countToScan; ++i) {
                if (restorer.itemIdAt(i) === restorer._pendingItemId) {
                    target = i
                    break
                }
            }
        }

        if (target >= 0) {
            restorer.cancel()
            restorer.focusRequested(target)
            return true
        }

        // A page request may resolve synchronously in a test double, so retire
        // this count and consume the budget before emitting the signal.
        if (restorer.canRequestPage && restorer._pageRequests < restorer.pageRequestLimit
                && restorer._lastRequestedCount !== restorer.count) {
            restorer._lastRequestedCount = restorer.count
            ++restorer._pageRequests
            restorer.pageRequested()
        }
        return false
    }
}
