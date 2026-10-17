import QtQuick

// Tier shim (spec §4.3), compat tier: hands a ListView or GridView a header or
// footer declared under `pragma ComponentBehavior: Bound`. Qt 6.4's item views
// create header and footer in a new context under the component's, and a
// bound component refuses that: the view logs "Component is not ready" and
// has no footer. Qt 6.5 passes the creation context instead.
//
// This file is not bound, so the view can create `component`. Its item creates
// the bound one with createObject(), which uses the creation context. The
// view's headerItem/footerItem is that host; it is a plain Item, not a focus
// scope, so focus reaches the created item as it would without the host.
//
// The host takes the created item's size and gives it none: a view does not
// size its header or footer either, so the item's own `width:` is what
// decides, as on the full tier. Assigning one from here would replace that
// binding with a number.
//
// `component` stays the one component whatever `sourceComponent` becomes, so
// the view is never told to build a new footer the way it is on the full
// tier, where the two are the same object. Each host follows the change
// itself.
QtObject {
    id: slot

    property Component sourceComponent: null
    readonly property Component component: Component {
        Item {
            id: host

            property Item content: null

            implicitWidth: host.content ? host.content.width : 0
            implicitHeight: host.content ? host.content.height : 0

            function rebuild() {
                const old = host.content;
                if (old) {
                    host.content = null;
                    old.visible = false;
                    old.parent = null;
                    old.destroy();
                }
                if (slot.sourceComponent !== null)
                    host.content = slot.sourceComponent.createObject(host);
            }

            Component.onCompleted: host.rebuild()

            Connections {
                target: slot

                function onSourceComponentChanged() {
                    host.rebuild();
                }
            }
        }
    }
}
