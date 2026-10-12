pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import StrmQt

// FilterSheet — narrow a library by its metadata: genre, year, studio, age
// rating, resolution and whatever else the controller offers.
//
// Like FilterBar, it decides nothing and stores nothing. The CONTROLLER owns
// the list of axes (`facets`), the values of each (`facetOptions(key)`) and
// what is picked; this renders them and calls back (`toggleFacetValue`,
// `clearFacet`, `clearFilters`). A new axis is a row in the controller's table
// and appears here without this file changing.
//
// Two panes: what to filter by on the left, its values on the right. A pick
// applies at once — the grid behind the scrim is the preview, and the count in
// the footer is the answer to "how many is that" before the sheet is closed.
// There is no Apply to forget.
//
// Keyboard and pad: each pane is a list that holds the keyboard itself, so
// Up/Down walk it, Left/Right cross between the panes (FocusNavigator finds
// the other one), Select ticks, Back closes. Hover lights a row and never
// moves the cursor (ARCHITECTURE.md, "Hover is not focus").
FocusScope {
    id: sheet

    // LibraryCtl, passed in for the same reason FilterBar's is.
    property var controller: null
    property bool opened: false

    // Arrows and Back stop at the sheet's edge instead of reaching the page
    // behind the scrim (FocusNavigator).
    property bool navModal: sheet.opened
    property Item navEntry: facetList

    signal dismissed

    // ── State ──────────────────────────────────────────────────────────────
    readonly property var facets: sheet.controller !== null ? sheet.controller.facets : []
    // The left-hand list is modelled on the KEYS, which change only with the
    // library, and each row looks its facet up. Modelled on `facets` itself it
    // was rebuilt by every tick — a tick changes a summary — and a rebuilt
    // list resets its cursor to the first row, which the sheet then read as
    // the user choosing Genre.
    property var facetKeys: []
    onFacetsChanged: {
        const keys = (sheet.facets || []).map(facet => String(facet.key));
        if (keys.join("\n") !== sheet.facetKeys.join("\n"))
            sheet.facetKeys = keys;
    }
    function facetFor(key: string): var {
        const list = sheet.facets || [];
        for (let i = 0; i < list.length; ++i) {
            if (list[i].key === key)
                return list[i];
        }
        return null;
    }
    property string activeKey: ""
    readonly property var activeFacet: {
        const list = sheet.facets || [];
        for (let i = 0; i < list.length; ++i) {
            if (list[i].key === sheet.activeKey)
                return list[i];
        }
        return null;
    }
    // Every value of the active facet, and the ones the search leaves.
    property var options: []
    property var rows: []
    // What is ticked, kept apart from `rows`: a tick must not rebuild the list
    // under the cursor, and a studio list is long enough for that to throw
    // the user back to its top on every pick.
    property var picked: ({})
    readonly property bool loading: sheet.controller !== null && sheet.activeKey.length > 0
                                    && sheet.optionsRevision >= 0
                                    && sheet.controller.facetLoading(sheet.activeKey)
    // Bumped to re-ask facetLoading(), which is a call and not a property.
    property int optionsRevision: 0
    // A long list gets a search field; a short one would only be pushed down.
    readonly property bool searchable: sheet.options.length > 14

    function open(): void {
        if (sheet.controller === null || (sheet.facets || []).length === 0)
            return;
        sheet.opened = true;
        if (sheet.activeFacet === null) {
            facetList.currentIndex = 0;
            sheet.activate(String(sheet.facets[0].key));
        } else {
            sheet.refreshOptions(true);
        }
        facetList.forceActiveFocus(Qt.OtherFocusReason);
    }

    function close(): void {
        if (!sheet.opened)
            return;
        sheet.opened = false;
        sheet.dismissed();
    }

    function activate(key: string): void {
        if (sheet.activeKey === key)
            return;
        sheet.activeKey = key;
        field.text = "";
        sheet.refreshOptions(true);
    }

    // `rebuild` false is a tick: same values, different marks.
    function refreshOptions(rebuild: bool): void {
        const fetched = sheet.controller !== null && sheet.activeKey.length > 0
                      ? sheet.controller.facetOptions(sheet.activeKey) : [];
        const marks = {};
        for (let i = 0; i < fetched.length; ++i) {
            if (fetched[i].selected === true)
                marks[String(fetched[i].value)] = true;
        }
        sheet.picked = marks;
        sheet.optionsRevision += 1;
        if (!rebuild && fetched.length === sheet.options.length)
            return;
        // What is already ticked leads, in the list's own order: on a studio
        // list of a thousand, the two that are on would otherwise be found by
        // scrolling. Only when the list is built — never under the cursor.
        const on = [];
        const off = [];
        for (let i = 0; i < fetched.length; ++i)
            (marks[String(fetched[i].value)] === true ? on : off).push(fetched[i]);
        sheet.options = on.concat(off);
        sheet.filterRows();
    }

    function filterRows(): void {
        const needle = field.text.trim().toLowerCase();
        if (needle.length === 0) {
            sheet.rows = sheet.options;
        } else {
            const out = [];
            for (let i = 0; i < sheet.options.length; ++i) {
                if (String(sheet.options[i].label).toLowerCase().indexOf(needle) >= 0)
                    out.push(sheet.options[i]);
            }
            sheet.rows = out;
        }
        optionList.currentIndex = sheet.rows.length > 0 ? 0 : -1;
    }

    function toggle(index: int): void {
        if (index < 0 || index >= sheet.rows.length || sheet.controller === null)
            return;
        sheet.controller.toggleFacetValue(sheet.activeKey, String(sheet.rows[index].value));
    }

    Connections {
        target: sheet.controller
        enabled: sheet.controller !== null

        function onFacetOptionsChanged(key) {
            if (key === sheet.activeKey)
                sheet.refreshOptions(false);
        }
        // A different library has different axes; a sheet left pointing at
        // one that is gone would list nothing.
        function onScopeChanged() {
            sheet.activeKey = "";
            sheet.options = [];
            sheet.rows = [];
            sheet.close();
        }
    }

    anchors.fill: parent
    // `opened` as well as the opacity: open() focuses the list in the same
    // call, and an item that is still invisible then does not take focus.
    visible: sheet.opened || sheet.opacity > 0.01
    enabled: sheet.opened
    opacity: sheet.opened ? 1.0 : 0.0

    Behavior on opacity {
        NumberAnimation { duration: Theme.animFastMs; easing.type: Theme.easeStandard }
    }

    Keys.onEscapePressed: event => {
        sheet.close();
        event.accepted = true;
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrimColor

        TapHandler {
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: sheet.close()
        }
    }

    Rectangle {
        id: surface

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.round(parent.height * 0.08)
        width: Math.min(parent.width - Theme.pageMarginValue * 2, Theme.scale(760))
        height: Math.min(parent.height - 2 * anchors.topMargin, Theme.scale(600))
        radius: Theme.radiusPanel
        color: Theme.surfaceOverlay
        border.width: 1
        border.color: Theme.hairline

        // Swallows clicks so one inside the sheet is not one on the scrim.
        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds }

        Text {
            id: heading

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacingValue
            text: qsTr("Filters")
            color: Theme.textPrimaryColor
            font.family: Theme.fontDisplay
            font.pixelSize: Theme.fontTitle
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        // ── Left: what to filter by ────────────────────────────────────────
        ListView {
            id: facetList
            objectName: "filterSheetFacets"

            anchors.top: heading.bottom
            anchors.bottom: footer.top
            anchors.left: parent.left
            anchors.margins: Theme.spacingTight
            width: Math.min(Theme.scale(250), Math.round(surface.width * 0.38))
            clip: true
            focus: true
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationWraps: false
            model: sheet.facetKeys

            onCurrentIndexChanged: {
                const keys = sheet.facetKeys;
                if (facetList.currentIndex >= 0 && facetList.currentIndex < keys.length)
                    sheet.activate(String(keys[facetList.currentIndex]));
            }

            // Select on an axis goes to its values, as Right does.
            Keys.onReturnPressed: event => { if (!event.isAutoRepeat) sheet.enterOptions(); }
            Keys.onEnterPressed: event => { if (!event.isAutoRepeat) sheet.enterOptions(); }

            ScrollBar.vertical: StrmScrollBar {}

            delegate: Item {
                id: facetRow

                required property int index
                required property string modelData

                readonly property var facet: sheet.facetFor(facetRow.modelData)
                readonly property bool current: facetList.currentIndex === facetRow.index
                readonly property bool active: facetRow.facet !== null
                                               && Number(facetRow.facet.count) > 0

                width: facetList.width
                height: Theme.controlHeightLarge + Theme.scale(8)

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Theme.scale(2)
                    radius: Theme.radiusChip
                    color: facetRow.current || facetHover.hovered ? Theme.hoverTint : "transparent"

                    FocusRing {
                        active: facetRow.current && facetList.activeFocus
                        radius: Theme.radiusChip
                    }
                }

                Column {
                    anchors.left: parent.left
                    anchors.right: chevron.left
                    anchors.leftMargin: Theme.spacingValue
                    anchors.rightMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scale(1)

                    Text {
                        width: parent.width
                        text: facetRow.facet !== null ? String(facetRow.facet.label) : ""
                        color: facetRow.active ? Theme.accentColor : Theme.textPrimaryColor
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontBodySize
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        visible: facetRow.active
                        text: facetRow.facet !== null ? String(facetRow.facet.summary) : ""
                        color: Theme.textTertiary
                        font.family: Theme.fontBody
                        font.pixelSize: Theme.fontCaption
                        elide: Text.ElideRight
                    }
                }

                StrmIcon {
                    id: chevron

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingTight
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron-right"
                    size: Theme.scale(14)
                    color: facetRow.current ? Theme.textSecondaryColor : Theme.textTertiary
                }

                HoverHandler {
                    id: facetHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: facetList.currentIndex = facetRow.index
                }
            }
        }

        Rectangle {
            id: divider

            anchors.top: facetList.top
            anchors.bottom: facetList.bottom
            anchors.left: facetList.right
            anchors.leftMargin: Theme.spacingTight
            width: 1
            color: Theme.hairline
        }

        // ── Right: its values ──────────────────────────────────────────────
        StrmSearchField {
            id: field
            objectName: "filterSheetField"

            anchors.top: heading.bottom
            anchors.left: divider.right
            anchors.right: parent.right
            anchors.margins: Theme.spacingTight
            visible: sheet.searchable
            height: sheet.searchable ? Theme.controlHeightLarge : 0
            implicitHeight: Theme.controlHeightLarge
            placeholderText: sheet.activeFacet !== null
                             ? qsTr("Find in %1…").arg(String(sheet.activeFacet.label).toLowerCase())
                             : ""

            property Item navDown: optionList

            onTextEdited: sheet.filterRows()
            onCleared: sheet.filterRows()
            onEscapePressed: sheet.close()
        }

        ListView {
            id: optionList
            objectName: "filterSheetOptions"

            anchors.top: field.bottom
            anchors.bottom: footer.top
            anchors.left: divider.right
            anchors.right: parent.right
            anchors.margins: Theme.spacingTight
            clip: true
            activeFocusOnTab: true
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationWraps: false
            reuseItems: true
            model: sheet.rows

            Keys.onReturnPressed: event => {
                if (!event.isAutoRepeat)
                    sheet.toggle(optionList.currentIndex);
            }
            Keys.onEnterPressed: event => {
                if (!event.isAutoRepeat)
                    sheet.toggle(optionList.currentIndex);
            }
            Keys.onSpacePressed: event => {
                if (!event.isAutoRepeat)
                    sheet.toggle(optionList.currentIndex);
            }

            ScrollBar.vertical: StrmScrollBar {}

            delegate: Item {
                id: row

                required property int index
                required property var modelData

                readonly property bool current: optionList.currentIndex === row.index
                readonly property bool ticked: sheet.picked[String(row.modelData.value)] === true
                readonly property bool single: sheet.activeFacet !== null
                                               && sheet.activeFacet.multi !== true

                width: optionList.width
                height: Theme.controlHeightLarge

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Theme.scale(2)
                    radius: Theme.radiusChip
                    color: row.current || rowHover.hovered ? Theme.hoverTint : "transparent"

                    FocusRing {
                        active: row.current && optionList.activeFocus
                        radius: Theme.radiusChip
                    }
                }

                // A box for "any of these", a dot for "one of these".
                Rectangle {
                    id: box

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scale(18)
                    height: width
                    radius: row.single ? width / 2 : Theme.scale(3)
                    color: row.ticked ? Theme.accentColor : "transparent"
                    border.width: row.ticked ? 0 : 1
                    border.color: row.current ? Theme.textSecondaryColor : Theme.textTertiary

                    StrmIcon {
                        anchors.centerIn: parent
                        visible: row.ticked
                        name: "check"
                        size: Theme.scale(14)
                        color: Theme.accentText
                    }
                }

                Text {
                    anchors.left: box.right
                    anchors.leftMargin: Theme.spacingValue
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingValue
                    anchors.verticalCenter: parent.verticalCenter
                    text: String(row.modelData.label)
                    color: row.ticked ? Theme.accentColor : Theme.textPrimaryColor
                    font.family: Theme.fontBody
                    font.pixelSize: Theme.fontBodySize
                    elide: Text.ElideRight
                }

                HoverHandler {
                    id: rowHover
                    cursorShape: Qt.PointingHandCursor
                }

                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: {
                        optionList.currentIndex = row.index;
                        sheet.toggle(row.index);
                    }
                }
            }
        }

        Text {
            anchors.centerIn: optionList
            width: optionList.width - Theme.spacingValue * 2
            visible: sheet.rows.length === 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: sheet.loading ? qsTr("Loading…")
                : sheet.options.length === 0 ? qsTr("Nothing to filter by here.")
                : qsTr("No match.")
            color: Theme.textTertiary
            font.family: Theme.fontBody
            font.pixelSize: Theme.fontSmall
        }

        // ── Footer: the answer so far, and the ways out ────────────────────
        Item {
            id: footer

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingValue
            height: Theme.controlHeight

            Text {
                anchors.left: parent.left
                anchors.right: actions.left
                anchors.rightMargin: Theme.spacingValue
                anchors.verticalCenter: parent.verticalCenter
                text: {
                    if (sheet.controller === null)
                        return "";
                    if (sheet.controller.loading === true)
                        return qsTr("Looking…");
                    const total = sheet.controller.model.totalRecordCount;
                    return total === 1 ? qsTr("1 title") : qsTr("%1 titles").arg(total);
                }
                color: Theme.textSecondaryColor
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }

            Row {
                id: actions

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingTight

                StrmButton {
                    id: clearOneButton
                    objectName: "filterSheetClearOne"

                    text: qsTr("Clear this")
                    variant: "ghost"
                    enabled: sheet.activeFacet !== null && Number(sheet.activeFacet.count) > 0
                    onClicked: sheet.controller.clearFacet(sheet.activeKey)
                }

                StrmButton {
                    id: clearAllButton
                    objectName: "filterSheetClearAll"

                    text: qsTr("Clear all")
                    variant: "ghost"
                    enabled: sheet.controller !== null && sheet.controller.filtered === true
                    onClicked: sheet.controller.clearFilters()
                }

                StrmButton {
                    id: doneButton
                    objectName: "filterSheetDone"

                    text: qsTr("Done")
                    iconName: "check"
                    variant: "primary"
                    onClicked: sheet.close()
                }
            }
        }
    }

    // To the values, not the search field above them: a pad cannot type, and
    // the field is one Up away for whoever can.
    function enterOptions(): void {
        if (optionList.count > 0)
            optionList.forceActiveFocus(Qt.TabFocusReason);
        else if (sheet.searchable)
            field.forceActiveFocus(Qt.TabFocusReason);
    }
}
