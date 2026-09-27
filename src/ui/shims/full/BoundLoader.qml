import QtQuick

// Tier shim (spec 2026-09-27 §4.3): the Loader for components declared under
// `pragma ComponentBehavior: Bound`. From Qt 6.5 on, Loader creates a bound
// component in its creation context itself.
Loader {
}
