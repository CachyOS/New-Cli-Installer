import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller
import "../../components/Format.js" as Fmt
import "../../components/Partitions.js" as Pt

// A proportional bar + a table of existing
// partitions, with Create / Edit / Delete / Revert / New Partition Table actions
// that accumulate an ordered operation list. PartitionPage applies the ops via
// applyPartitionOps() and then finalizes mount selections from the table.
ColumnLayout {
    id: root

    // Disk picked above (e.g. "/dev/sda") and the full inventory (for disk size).
    property string parentDevice: ""
    property var blockDevices: []

    // ---- outputs consumed by PartitionPage.buildPlan() ----
    // Ordered partition-table edits ({ kind, … } maps) for applyPartitionOps().
    property var ops: []
    // Mount rows: { source:"existing"|"created", device, mountpoint, fstype,
    // formatRequested, mountOpts }. "created" rows get their device from the
    // applyPartitionOps() return value.
    property var mounts: []

    spacing: Style.marginM

    readonly property real diskTotalBytes: {
        for (var i = 0; i < root.blockDevices.length; ++i) {
            if (root.blockDevices[i].name === root.parentDevice)
                return root.blockDevices[i].sizeBytes;
        }
        return 0;
    }

    // On-disk baseline, re-queried whenever the disk changes (non-destructive).
    readonly property var partitions: parentDevice === ""
        ? [] : InstallerBackend.partitionsForDevice(parentDevice)
    readonly property var freeRegions: parentDevice === ""
        ? [] : InstallerBackend.freeSpaceRegions(parentDevice)

    property int selectedNumber: -1

    // Per-existing-partition editable state, keyed by partition number.
    // { mountpoint, mountOpts, deleted, format, fstype, label(null=keep),
    //   wantBoot, wantEsp, resizeBytes(0=keep) }
    property var existingState: ({})
    // Pending new partitions (in free space): { localId, startBytes, endBytes,
    // sizeBytes, fstype, label, flags, mountpoint, mountOpts }.
    property var created: []
    property int _nextCreatedId: 1
    // When set ("gpt"/"msdos") the whole disk is re-tabled (wipes everything).
    property string newTableType: ""

    function _hasFlag(flags, name) {
        return (flags || "").split(",").map(function(f) { return f.trim(); }).indexOf(name) !== -1;
    }

    // dm-crypt mapper name derived from the mountpoint, e.g. "/" → cryptroot,
    // "/home" → crypthome (no /dev/mapper/ prefix).
    function _mapperName(mountpoint) {
        if (mountpoint === "/" || mountpoint === "") return "cryptroot";
        var base = mountpoint.replace(/^\/+/, "").replace(/\//g, "_");
        return "crypt" + (base || "root");
    }

    function _stateFor(p) {
        if (!root.existingState[p.number]) {
            root.existingState[p.number] = {
                mountpoint: "", mountOpts: "", deleted: false,
                format: false, fstype: p.fstype || "btrfs",
                label: null, wantBoot: root._hasFlag(p.flags, "boot"),
                wantEsp: root._hasFlag(p.flags, "esp"), resizeBytes: 0,
            };
        }
        return root.existingState[p.number];
    }
    function _commitState() {
        root.existingState = Object.assign({}, root.existingState);  // trigger reactivity
        recompute();
    }

    function _partByNumber(number) {
        for (var i = 0; i < root.partitions.length; ++i)
            if (root.partitions[i].number === number) return root.partitions[i];
        return null;
    }

    // Re-derive the op + mount lists from the working state.
    function recompute() {
        var ops = [];
        var mounts = [];

        if (root.newTableType !== "") {
            // A fresh table wipes the disk; only created partitions survive.
            ops.push({ kind: "newtable", tableType: root.newTableType });
        } else {
            // Deletes first (they renumber the table), then resizes, flags, labels.
            for (var i = 0; i < root.partitions.length; ++i) {
                var p = root.partitions[i];
                var s = root.existingState[p.number];
                if (!s || !s.deleted) continue;
                ops.push({ kind: "delete", number: p.number });
            }
            for (var j = 0; j < root.partitions.length; ++j) {
                var rp = root.partitions[j];
                var rs = root.existingState[rp.number];
                if (!rs || rs.deleted) continue;
                if (rs.resizeBytes > 0 && rs.resizeBytes < rp.sizeBytes) {
                    ops.push({ kind: "resize", number: rp.number, fstype: rp.fstype,
                               sizeBytes: rs.resizeBytes, endBytes: rp.startBytes + rs.resizeBytes - 1 });
                }
                if (rs.wantBoot !== root._hasFlag(rp.flags, "boot"))
                    ops.push({ kind: "setflag", number: rp.number, flags: "boot", flagState: rs.wantBoot });
                if (rs.wantEsp !== root._hasFlag(rp.flags, "esp"))
                    ops.push({ kind: "setflag", number: rp.number, flags: "esp", flagState: rs.wantEsp });
                if (rs.label !== null)
                    ops.push({ kind: "setlabel", number: rp.number, fstype: rs.format ? rs.fstype : rp.fstype, label: rs.label });
                if (rs.mountpoint !== "") {
                    mounts.push({ source: "existing", device: rp.device, mountpoint: rs.mountpoint,
                                  fstype: rs.format ? rs.fstype : rp.fstype,
                                  formatRequested: rs.format, mountOpts: rs.mountOpts,
                                  isEsp: rs.wantEsp || root._hasFlag(rp.flags, "esp") });
                }
            }
        }

        // Creates go last so the last one is applyPartitionOps()'s returned target.
        for (var k = 0; k < root.created.length; ++k) {
            var c = root.created[k];
            // For an encrypted partition the raw device is left unformatted; the
            // filesystem is created on the LUKS mapper at install time, so the
            // mount carries formatRequested + the luks details.
            ops.push({ kind: "create", startBytes: c.startBytes, endBytes: c.endBytes,
                       fstype: c.luks ? "" : c.fstype, flags: c.flags, label: c.label });
            if (c.mountpoint !== "") {
                mounts.push({ source: "created", localId: c.localId, mountpoint: c.mountpoint,
                              fstype: c.fstype, formatRequested: !!c.luks, mountOpts: c.mountOpts,
                              isEsp: root._hasFlag(c.flags, "esp"),
                              luks: !!c.luks, luksPassphrase: c.luksPassphrase || "",
                              luksMapperName: c.luksMapperName || "" });
            }
        }

        root.ops = ops;
        root.mounts = mounts;
    }

    function revertAll() {
        root.existingState = ({});
        root.created = [];
        root.newTableType = "";
        root.selectedNumber = -1;
        recompute();
    }

    onParentDeviceChanged: revertAll()
    onPartitionsChanged: recompute()

    // A display row's summary of pending changes, for the table's status column.
    function _rowStatus(p) {
        if (root.newTableType !== "") return "will be wiped";
        var s = root.existingState[p.number];
        if (!s) return "keep";
        if (s.deleted) return "DELETE";
        var bits = [];
        if (s.resizeBytes > 0 && s.resizeBytes < p.sizeBytes) bits.push("resize→" + Fmt.formatSize(s.resizeBytes));
        if (s.format) bits.push("format " + s.fstype);
        if (s.mountpoint !== "") bits.push("mount " + s.mountpoint);
        return bits.length ? bits.join(", ") : "keep";
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: {
            if (root.parentDevice === "")
                return "Pick a target disk above.";
            if (root.partitions.length === 0 && !InstallerBackend.isRoot)
                return "The installer must run as root to read the partition table of "
                       + root.parentDevice + ". Re-run with root privileges to edit partitions.";
            return "Edit the partition table on " + root.parentDevice
                   + ". Click a partition to select it, then use the actions below. Exactly one partition must be mounted at \"/\".";
        }
        font.pixelSize: Style.fontSizeM
        color: Color.mOnSurfaceVariant
    }

    PartitionBar {
        Layout.fillWidth: true
        visible: root.partitions.length > 0 && root.newTableType === ""
        partitions: root.partitions
        freeRegions: root.freeRegions
        totalBytes: root.diskTotalBytes
        selectedNumber: root.selectedNumber
        onPartitionClicked: function(number) { root.selectedNumber = number; }
    }

    // Action bar.
    Flow {
        Layout.fillWidth: true
        spacing: Style.marginS

        NButton {
            text: "Create"
            enabled: root.freeRegions.length > 0 || root.newTableType !== ""
            onClicked: createDialog.openFor()
        }
        NButton {
            text: "Edit"
            outlined: true
            enabled: root.selectedNumber !== -1 && root.newTableType === ""
            onClicked: { var p = root._partByNumber(root.selectedNumber); if (p) editDialog.openFor(p); }
        }
        NButton {
            text: "Delete"
            outlined: true
            enabled: root.selectedNumber !== -1 && root.newTableType === ""
            onClicked: {
                var p = root._partByNumber(root.selectedNumber);
                if (p) { root._stateFor(p).deleted = true; root._stateFor(p).mountpoint = ""; root._commitState(); }
            }
        }
        NButton {
            text: "New Partition Table"
            outlined: true
            onClicked: tableDialog.open()
        }
        NButton {
            text: "Revert"
            outlined: true
            enabled: root.ops.length > 0 || root.newTableType !== ""
            onClicked: root.revertAll()
        }
    }

    // Existing-partition table.
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.partitions.length > 0 && root.newTableType === ""
        spacing: Style.marginXS

        RowLayout {
            Layout.fillWidth: true
            spacing: Style.marginS
            component Hdr: Text {
                font.pixelSize: Style.fontSizeS
                font.weight: Style.fontWeightSemiBold
                color: Color.mOnSurfaceVariant
            }
            Hdr { text: "Partition";  Layout.preferredWidth: 160 }
            Hdr { text: "Type";       Layout.preferredWidth: 120 }
            Hdr { text: "Filesystem"; Layout.preferredWidth: 70 }
            Hdr { text: "Label";      Layout.preferredWidth: 90 }
            Hdr { text: "Pending";    Layout.fillWidth: true }
        }

        Repeater {
            model: root.partitions
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: rowLayout.implicitHeight + Style.marginS
                radius: Style.radiusS
                color: modelData.number === root.selectedNumber ? Qt.alpha(Color.mPrimary, 0.12) : "transparent"

                MouseArea { anchors.fill: parent; onClicked: root.selectedNumber = modelData.number }

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: Style.marginXS
                    spacing: Style.marginS
                    ColumnLayout {
                        Layout.preferredWidth: 160
                        spacing: 0
                        Text { text: modelData.device; font.pixelSize: Style.fontSizeS; color: Color.mOnSurface; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        Text {
                            text: Fmt.formatSize(modelData.sizeBytes)
                                  + ((modelData.usedBytes || 0) > 0 ? " · " + Fmt.formatSize(modelData.usedBytes) + " used" : "")
                            font.pixelSize: Style.fontSizeXS; color: Color.mOnSurfaceVariant
                        }
                    }
                    Text { text: modelData.partType || modelData.fstype || "—"; Layout.preferredWidth: 120; font.pixelSize: Style.fontSizeXS; color: Color.mOnSurfaceVariant; elide: Text.ElideRight }
                    Text { text: modelData.fstype || "—"; Layout.preferredWidth: 70; font.pixelSize: Style.fontSizeS; color: Color.mOnSurface }
                    Text { text: modelData.label || "—"; Layout.preferredWidth: 90; font.pixelSize: Style.fontSizeXS; color: Color.mOnSurfaceVariant; elide: Text.ElideRight }
                    Text {
                        Layout.fillWidth: true
                        text: root._rowStatus(modelData)
                        font.pixelSize: Style.fontSizeXS
                        color: root._rowStatus(modelData) === "DELETE" ? Color.mError
                             : (root._rowStatus(modelData) === "keep" ? Color.mOnSurfaceVariant : Color.mPrimary)
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // Pending-creates list.
    ColumnLayout {
        Layout.fillWidth: true
        visible: root.created.length > 0
        spacing: Style.marginXS
        Text { text: "New partitions"; font.pixelSize: Style.fontSizeS; font.weight: Style.fontWeightSemiBold; color: Color.mOnSurfaceVariant }
        Repeater {
            model: root.created
            delegate: RowLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                spacing: Style.marginS
                Text {
                    Layout.fillWidth: true
                    text: "+ " + Fmt.formatSize(modelData.sizeBytes) + " " + modelData.fstype
                          + (modelData.mountpoint ? " @ " + modelData.mountpoint : "")
                          + (modelData.flags ? " [" + modelData.flags + "]" : "")
                    font.pixelSize: Style.fontSizeS
                    color: Color.mPrimary
                }
                NButton {
                    text: "Remove"
                    outlined: true
                    fontSize: Style.fontSizeXS
                    onClicked: {
                        var next = root.created.slice();
                        next.splice(index, 1);
                        root.created = next;
                        recompute();
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.newTableType !== ""
        wrapMode: Text.Wrap
        text: "A new " + root.newTableType.toUpperCase() + " partition table will be written to "
              + root.parentDevice + ", erasing all existing partitions. Use Create to add partitions, or Revert to cancel."
        font.pixelSize: Style.fontSizeS
        font.weight: Style.fontWeightSemiBold
        color: Color.mError
    }

    // ---------------- Create dialog ----------------
    PartitionDialog {
        id: createDialog
        title: "Create partition"
        confirmText: "Add"
        width: 460

        property int regionIndex: 0
        readonly property var region: root.newTableType !== ""
            ? { startBytes: 0, endBytes: root.diskTotalBytes - 1, sizeBytes: root.diskTotalBytes }
            : (root.freeRegions[regionIndex] || null)

        function openFor() {
            createDialog.regionIndex = 0;
            cSize.text = "20";
            cFs.currentKey = "btrfs";
            cMount.text = "/";
            cLabel.text = "";
            cBoot.checked = false;
            cEsp.checked = false;
            cLuks.checked = false;
            cLuksPass.text = "";
            createDialog.open();
        }

        onConfirmed: {
            var reg = createDialog.region;
            if (!reg) { createDialog.errorText = "No free region selected."; return; }
            var bytes = Pt.parseGiB(cSize.text);
            if (bytes <= 0) { createDialog.errorText = "Enter a positive size."; return; }
            if (bytes > reg.sizeBytes) { createDialog.errorText = "Size exceeds the free region (" + Fmt.formatSize(reg.sizeBytes) + ")."; return; }
            if (cLuks.checked && cLuksPass.text.length === 0) { createDialog.errorText = "Enter a LUKS passphrase."; return; }
            var flags = [];
            if (cBoot.checked) flags.push("boot");
            if (cEsp.checked) flags.push("esp");
            var next = root.created.slice();
            next.push({
                localId: root._nextCreatedId++,
                startBytes: reg.startBytes,
                endBytes: reg.startBytes + bytes - 1,
                sizeBytes: bytes,
                fstype: cFs.currentKey,
                label: cLabel.text,
                flags: flags.join(","),
                mountpoint: cMount.text,
                mountOpts: "",
                luks: cLuks.checked,
                luksPassphrase: cLuks.checked ? cLuksPass.text : "",
                luksMapperName: cLuks.checked ? root._mapperName(cMount.text) : "",
            });
            root.created = next;
            recompute();
            createDialog.close();
        }

        NComboBox {
            Layout.fillWidth: true
            label: "Free region"
            visible: root.newTableType === "" && root.freeRegions.length > 1
            model: {
                var m = [];
                for (var i = 0; i < root.freeRegions.length; ++i)
                    m.push({ key: String(i), name: Fmt.formatSize(root.freeRegions[i].sizeBytes) + " free" });
                return m;
            }
            currentKey: String(createDialog.regionIndex)
            onSelected: function(key) { createDialog.regionIndex = parseInt(key); }
        }
        Text {
            Layout.fillWidth: true
            text: createDialog.region
                  ? "Up to " + Fmt.formatSize(createDialog.region.sizeBytes) + " available."
                  : "No free space available."
            font.pixelSize: Style.fontSizeXS; color: Color.mOnSurfaceVariant
        }
        NTextInput { id: cSize; Layout.fillWidth: true; label: "Size (GiB)"; text: "20" }
        NComboBox { id: cFs; Layout.fillWidth: true; label: "Filesystem"; model: Pt.fsComboModel(); currentKey: "btrfs" }
        NTextInput { id: cMount; Layout.fillWidth: true; label: "Mountpoint"; placeholderText: "/ or /home (blank = none)" }
        NTextInput { id: cLabel; Layout.fillWidth: true; label: "Filesystem label (optional)" }
        RowLayout {
            Layout.fillWidth: true; spacing: Style.marginL
            NCheckbox { id: cBoot; label: "boot flag" }
            NCheckbox { id: cEsp;  label: "esp flag" }
        }
        NCheckbox { id: cLuks; label: "Encrypt (LUKS)" }
        PasswordField {
            id: cLuksPass
            Layout.fillWidth: true
            label: "LUKS passphrase"
            visible: cLuks.checked
        }
    }

    // ---------------- Edit existing dialog ----------------
    PartitionDialog {
        id: editDialog
        title: editDialog.part ? "Edit " + editDialog.part.device : "Edit partition"
        confirmText: "Apply"
        width: 460

        property var part: null
        function openFor(p) {
            editDialog.part = p;
            var s = root._stateFor(p);
            eFormat.checked = s.format;
            eFs.currentKey = s.fstype;
            eMount.text = s.mountpoint;
            eOpts.text = s.mountOpts;
            eLabel.text = s.label === null ? "" : s.label;
            eBoot.checked = s.wantBoot;
            eEsp.checked = s.wantEsp;
            eResize.text = Pt.bytesToGiB(s.resizeBytes > 0 ? s.resizeBytes : p.sizeBytes);
            editDialog.open();
        }

        onConfirmed: {
            var p = editDialog.part;
            if (!p) { editDialog.close(); return; }
            var s = root._stateFor(p);
            s.format = eFormat.checked;
            s.fstype = eFs.currentKey;
            s.mountpoint = eMount.text;
            s.mountOpts = eOpts.text;
            s.label = eLabel.text === "" ? null : eLabel.text;
            s.wantBoot = eBoot.checked;
            s.wantEsp = eEsp.checked;
            var newBytes = Pt.parseGiB(eResize.text);
            if (newBytes > 0 && newBytes < p.sizeBytes) {
                if (!Pt.canShrink(p.fstype)) { editDialog.errorText = p.fstype + " filesystems cannot be shrunk."; return; }
                s.resizeBytes = newBytes;
            } else {
                s.resizeBytes = 0;
            }
            root._commitState();
            editDialog.close();
        }

        NCheckbox { id: eFormat; label: "Format (erase contents)" }
        NComboBox { id: eFs; Layout.fillWidth: true; label: "Filesystem"; enabled: eFormat.checked; model: Pt.fsComboModel(); currentKey: "btrfs" }
        NTextInput { id: eMount; Layout.fillWidth: true; label: "Mountpoint"; placeholderText: "/ or /home (blank = skip)" }
        NTextInput { id: eOpts;  Layout.fillWidth: true; label: "Mount options (optional)" }
        NTextInput { id: eLabel; Layout.fillWidth: true; label: "Filesystem label (blank = keep)" }
        NTextInput {
            id: eResize; Layout.fillWidth: true
            label: "Resize to (GiB)"
            description: editDialog.part ? "Current: " + Fmt.formatSize(editDialog.part.sizeBytes) + ". Shrink only (ext*/ntfs/btrfs)." : ""
        }
        RowLayout {
            Layout.fillWidth: true; spacing: Style.marginL
            NCheckbox { id: eBoot; label: "boot flag" }
            NCheckbox { id: eEsp;  label: "esp flag" }
        }
    }

    // ---------------- New partition table dialog ----------------
    PartitionDialog {
        id: tableDialog
        title: "New partition table"
        confirmText: "Wipe & create"
        confirmColor: Color.mError
        confirmTextColor: Color.mOnError
        width: 420

        onConfirmed: {
            root.existingState = ({});
            root.created = [];
            root.selectedNumber = -1;
            root.newTableType = tType.currentKey;
            recompute();
            tableDialog.close();
        }

        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: "This erases ALL partitions on " + root.parentDevice + ". GPT is recommended for UEFI; msdos (MBR) for legacy BIOS."
            font.pixelSize: Style.fontSizeS; color: Color.mError
        }
        NComboBox {
            id: tType; Layout.fillWidth: true; label: "Table type"
            model: [{key:"gpt",name:"GPT (UEFI, recommended)"},{key:"msdos",name:"msdos / MBR (legacy BIOS)"}]
            currentKey: "gpt"
        }
    }
}
