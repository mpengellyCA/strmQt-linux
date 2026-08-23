pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import StrmQt

// MappedShortcut — a shortcut defined by an InputMap action id rather than by a
// key string (ARCHITECTURE.md §5).
//
// `InputMap` is the single source of truth for every binding, and this is what
// makes that true in QML: nothing declares `Shortcut { sequence: "F2" }`, so a
// rebind takes effect everywhere at once, including on the gamepad, which
// resolves a pad button to an action id and synthesizes whatever that action is
// currently bound to.
//
// ── Two Shortcuts, not one ─────────────────────────────────────────────────
// A sequence a text field would use itself ("/", "F", "S", Space, Backspace)
// stands down while a field has focus. A chord or a function key never can be
// typed and must keep working — including inside a search box, which is the
// whole point of Ctrl+K.
//
// The split asks `InputMap.isTypableSequence()`, which resolves the key. It
// used to be `sequence.length !== 1`, and that rule reads "Space" — five
// characters, one space bar — as a chord, which is the opposite of what the
// paragraph above says the two halves are for. `music.playPause` binds Space,
// so the one action most likely to be live while a name is being typed was in
// the half that never stands down.
//
// It did not eat spaces, and the honest reason is worth recording: an editable
// QQuickTextInput accepts the ShortcutOverride event for every no-modifier (or
// Shift-only) key below Qt::Key_Escape, so Qt hands the key back before the
// shortcut can fire — measured on TextInput, TextField and TextArea in
// tests/unit/tst_shortcut_typing.cpp. That protection is Qt's, not this
// component's, and it lapses the moment a field is read-only. `_editingText` is
// this app's own statement of the rule, and a binding can only benefit from it
// if it is sorted by what the key IS rather than by how long its name is.
//
// ── Where it lives ─────────────────────────────────────────────────────────
// It began as an inline `component` in Main.qml, which was right while Main.qml
// was the only place shortcuts were declared. Music has its own input context
// now (MUSIC.md §7) and its keys belong to the pages that can carry them out —
// "favourite what is selected" needs the row under the cursor, which Main.qml
// cannot see. Copying the typable/chord split into three pages is exactly the
// drift TrackRow and PlaylistPicker were extracted to stop.
//
// A page-owned instance must gate itself on the page being on screen:
// `Shortcut` is window-scoped, and a StackView keeps covered pages alive.
// `active: page.visible` is the whole of it.
Item {
    id: mapped

    property string actionId: ""
    // Used only until InputMap grows the action. An action that IS in the map
    // always wins, so a fallback cannot outlive a real binding.
    property var fallback: []
    property bool active: true

    signal activated

    // Reading Input.actions — a notifying property — is what makes every
    // sequence below update live when a binding changes.
    readonly property var _sequences: {
        const list = Input.actions;
        for (let i = 0; i < list.length; ++i) {
            if (list[i].actionId === mapped.actionId && list[i].sequences.length > 0)
                return list[i].sequences;
        }
        return mapped.fallback;
    }

    // True while a text input owns the keyboard — letter shortcuts must not
    // fire then. `cursorPosition` is the property every text-editing item has
    // and nothing else does.
    readonly property bool _editingText: {
        const item = mapped.Window.activeFocusItem;
        return item !== null && item.cursorPosition !== undefined;
    }

    Shortcut {
        sequences: mapped._sequences.filter(s => !Input.isTypableSequence(s))
        enabled: mapped.active
        onActivated: {
            Input.noteInput("keyboard");
            mapped.activated();
        }
    }

    Shortcut {
        sequences: mapped._sequences.filter(s => Input.isTypableSequence(s))
        enabled: mapped.active && !mapped._editingText
        onActivated: {
            Input.noteInput("keyboard");
            mapped.activated();
        }
    }
}
