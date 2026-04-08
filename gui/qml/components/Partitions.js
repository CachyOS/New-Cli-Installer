.pragma library

// Filesystems the Create/Edit dialogs offer, as the { key, name } model NComboBox
// expects. Single source of truth for the manual editor's fs choices. zfs is
// intentionally absent: a zfs root is a pool built via the dedicated ZFS flow on
// PartitionPage, not a plain single-partition filesystem the manual editor formats.
function fsComboModel() {
    return [
        { key: "btrfs", name: "btrfs" },
        { key: "xfs",   name: "xfs" },
        { key: "ext4",  name: "ext4" },
        { key: "f2fs",  name: "f2fs" },
        { key: "vfat",  name: "vfat" },
    ];
}

// Filesystems gucc can shrink (resize2fs / ntfsresize / btrfs). Everything else
// (xfs, vfat, swap, …) can only grow or not resize at all.
function canShrink(fstype) {
    return ["ext2", "ext3", "ext4", "ntfs", "btrfs"].indexOf(fstype) !== -1;
}

// Parse a GiB text field into bytes (0 when blank/invalid).
function parseGiB(text) {
    var v = parseFloat(text);
    return isNaN(v) ? 0 : Math.round(v * 1024 * 1024 * 1024);
}

// Bytes → a one-decimal GiB string suitable for prefilling a size input.
function bytesToGiB(bytes) {
    return (bytes / (1024 * 1024 * 1024)).toFixed(1);
}

// Stable colour per filesystem family so the same fs reads the same in every bar.
// Returns "" for unknown types so callers can fall back to a theme colour.
function fsColor(fstype) {
    switch (fstype) {
    case "ext2": case "ext3": case "ext4":   return "#4F86C6";
    case "btrfs":                            return "#C6724F";
    case "xfs":                              return "#7E57C2";
    case "ntfs":                             return "#4FAF6A";
    case "vfat": case "fat16": case "fat32": return "#C6A24F";
    case "swap": case "linux-swap":          return "#9E9E9E";
    default:                                 return "";
    }
}
