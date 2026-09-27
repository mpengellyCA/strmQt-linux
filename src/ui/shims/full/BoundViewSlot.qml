import QtQuick

// Tier shim (spec 2026-09-27 §4.3): hands a ListView or GridView a header or
// footer declared under `pragma ComponentBehavior: Bound`. From Qt 6.5 on, the
// views create a bound component in its creation context themselves, so
// `component` is the component itself.
QtObject {
    property Component sourceComponent: null
    readonly property Component component: sourceComponent
}
