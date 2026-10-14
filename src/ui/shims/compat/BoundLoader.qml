import QtQuick

// Tier shim (spec §4.3), compat tier: a Loader for components declared under
// `pragma ComponentBehavior: Bound`. Qt 6.4's Loader creates its item in a new
// context under the component's, and a bound component refuses that ("Cannot
// instantiate bound component outside its creation context"): nothing loads.
// Qt 6.5's Loader passes the creation context instead. createObject() uses the
// creation context on every Qt, so this creates the item with it.
//
// It covers what the callers use: active, sourceComponent, item, loaded(),
// focus scope, and Loader's sizing (a loader given a size sizes its item;
// otherwise the loader takes the item's size).
//
// "The item's size" is its width, not its implicitWidth, and that is Loader's
// rule rather than a shortcut taken here: a card that declares `width: 90`
// over wider content makes an unsized Loader 90 wide. A sized loader's item
// keeps the loader's size even when it declares a width binding of its own —
// Loader's does too, so the assignment in fit() takes nothing away from it.
// Both are held against the full tier's Loader by tst_crate_tokens, which runs
// the same expectations in either build.
//
// Two things a Loader does are left out because nothing reads them: a sized
// Loader reports its item's implicit size as its own implicit size (this one
// reports its width), and a Loader made inactive keeps the size its last item
// gave it (this one goes to 0 x 0).
FocusScope {
    id: loader

    property bool active: true
    property Component sourceComponent: null
    readonly property Item item: current.item

    signal loaded()

    implicitWidth: current.item ? current.item.width : 0
    implicitHeight: current.item ? current.item.height : 0

    // Not sized from outside, width follows implicitWidth, which is the item's
    // width; anchored or sized, the two differ and the item takes the loader's.
    // Written through current.item, not a local: Qt 6.4's qmlcachegen crashes
    // on `local.width = loader.width` here.
    function fit() {
        if (current.item === null)
            return;
        if (loader.width !== loader.implicitWidth)
            current.item.width = loader.width;
        if (loader.height !== loader.implicitHeight)
            current.item.height = loader.height;
    }

    function reload() {
        if (!current.ready)
            return;
        const old = current.item;
        if (old) {
            current.item = null;
            old.visible = false;
            old.parent = null;
            old.destroy();
        }
        if (!loader.active || loader.sourceComponent === null)
            return;
        // With no item, a loader that is not sized from outside is 0 x 0.
        const initial = {};
        if (loader.width !== 0)
            initial.width = loader.width;
        if (loader.height !== 0)
            initial.height = loader.height;
        const created = loader.sourceComponent.createObject(loader, initial);
        if (!created)
            return;
        current.item = created;
        loader.fit();
        loader.loaded();
    }

    onWidthChanged: loader.fit()
    onHeightChanged: loader.fit()
    onImplicitWidthChanged: loader.fit()
    onImplicitHeightChanged: loader.fit()
    onActiveChanged: loader.reload()
    onSourceComponentChanged: loader.reload()
    Component.onCompleted: {
        current.ready = true;
        loader.reload();
    }

    QtObject {
        id: current

        property Item item: null
        property bool ready: false
    }
}
