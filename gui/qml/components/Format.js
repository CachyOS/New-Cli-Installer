.pragma library

// Human-readable size in GiB, e.g. 5368709120 -> "5.0 GiB".
function formatSize(bytes) {
    return (bytes / (1024 * 1024 * 1024)).toFixed(1) + " GiB";
}

// Turns a flat string list into the { key, name } model NComboBox expects.
function toComboModel(list) {
    var result = [];
    for (var i = 0; i < list.length; ++i)
        result.push({ key: list[i], name: list[i] });
    return result;
}
