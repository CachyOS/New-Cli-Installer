.pragma library

// Helpers for the Packages page. The `groups` argument is the recursive tree of
// netinstall group maps emitted by InstallerBackend.netinstallGroupsReady():
//   { name, description, icon, selected, hidden, critical, bundle,
//     packages: [..], subgroups: [..] }
//
// Checkable units are groups and subgroups (keyed by their index path, e.g.
// "0" or "0/1"). Individual packages are shown as read-only leaves that follow
// their group's state, mirroring the Calamares netinstall tree.

function _key(prefix, i) {
    return prefix === "" ? String(i) : prefix + "/" + i;
}

// Initial selection map: a group is checked when it ships `selected` or `critical`.
function defaultSelection(groups) {
    var sel = {};
    function walk(list, prefix) {
        for (var i = 0; i < list.length; ++i) {
            var g = list[i];
            var key = _key(prefix, i);
            sel[key] = (g.selected === true) || (g.critical === true);
            if (g.subgroups && g.subgroups.length)
                walk(g.subgroups, key);
        }
    }
    walk(groups, "");
    return sel;
}

function nodeAt(groups, key) {
    var parts = key.split("/");
    var node = null;
    var list = groups;
    for (var i = 0; i < parts.length; ++i) {
        node = list[parseInt(parts[i], 10)];
        list = (node && node.subgroups) ? node.subgroups : [];
    }
    return node;
}

// Returns a fresh selection map with `key` set to `value`. When `cascade` is
// true, every descendant subgroup is set to `value` too (used by the simple grid
// so toggling a bundle card flips the whole group).
function setSelected(groups, sel, key, value, cascade) {
    var out = {};
    for (var k in sel)
        out[k] = sel[k];
    out[key] = value;
    if (cascade) {
        var node = nodeAt(groups, key);
        function walk(list, prefix) {
            for (var i = 0; i < list.length; ++i) {
                var k2 = _key(prefix, i);
                out[k2] = value;
                if (list[i].subgroups && list[i].subgroups.length)
                    walk(list[i].subgroups, k2);
            }
        }
        if (node && node.subgroups)
            walk(node.subgroups, key);
    }
    return out;
}

// True when a group and all of its descendants are selected (drives the grid
// card's "on" highlight).
function groupFullySelected(groups, sel, key) {
    if (!sel[key])
        return false;
    var node = nodeAt(groups, key);
    var ok = true;
    function walk(list, prefix) {
        for (var i = 0; i < list.length; ++i) {
            var k2 = _key(prefix, i);
            if (!sel[k2])
                ok = false;
            if (list[i].subgroups && list[i].subgroups.length)
                walk(list[i].subgroups, k2);
        }
    }
    if (node && node.subgroups)
        walk(node.subgroups, key);
    return ok;
}

function _matches(text, q) {
    return String(text).toLowerCase().indexOf(q) !== -1;
}

function _groupMatches(g, q) {
    if (q === "")
        return true;
    if (_matches(g.name, q) || _matches(g.description, q))
        return true;
    if (g.packages)
        for (var i = 0; i < g.packages.length; ++i)
            if (_matches(g.packages[i], q))
                return true;
    if (g.subgroups)
        for (var j = 0; j < g.subgroups.length; ++j)
            if (_groupMatches(g.subgroups[j], q))
                return true;
    return false;
}

// Bundle cards for the simple grid: top-level groups flagged `bundle`.
function bundleRows(groups) {
    var rows = [];
    for (var i = 0; i < groups.length; ++i) {
        if (groups[i].bundle === true)
            rows.push({ group: groups[i], key: String(i) });
    }
    return rows;
}

// Flatten the tree into display rows for the advanced view, honouring expand
// state and the search query. Hidden groups are never emitted. A non-empty
// search forces everything matching to expand.
function flatten(groups, sel, expanded, search) {
    var rows = [];
    var q = String(search || "").toLowerCase().trim();
    function walk(list, prefix, depth) {
        for (var i = 0; i < list.length; ++i) {
            var g = list[i];
            if (g.hidden === true)
                continue;
            if (!_groupMatches(g, q))
                continue;
            var key = _key(prefix, i);
            var isOpen = (q !== "") || (expanded[key] === true);
            rows.push({
                kind: depth === 0 ? "group" : "subgroup",
                depth: depth,
                name: g.name,
                description: g.description,
                icon: g.icon || "",
                key: key,
                checked: sel[key] === true,
                critical: g.critical === true,
                expanded: isOpen,
                checkable: true
            });
            if (isOpen) {
                if (g.packages) {
                    for (var p = 0; p < g.packages.length; ++p) {
                        var pkg = g.packages[p];
                        if (q !== "" && !_matches(pkg, q)
                            && !_matches(g.name, q) && !_matches(g.description, q))
                            continue;
                        rows.push({
                            kind: "package",
                            depth: depth + 1,
                            name: pkg,
                            description: "",
                            icon: "",
                            key: key + ":pkg:" + p,
                            checked: sel[key] === true,
                            critical: false,
                            expanded: false,
                            checkable: false
                        });
                    }
                }
                if (g.subgroups)
                    walk(g.subgroups, key, depth + 1);
            }
        }
    }
    walk(groups, "", 0);
    return rows;
}

// Flatten every package belonging to a selected group/subgroup (deduplicated).
// A selected parent contributes its own packages; subgroups contribute only when
// independently selected — matching the Calamares per-group checkboxes.
function collectPackages(groups, sel) {
    var seen = {};
    var out = [];
    function add(p) {
        if (!seen[p]) {
            seen[p] = true;
            out.push(p);
        }
    }
    function walk(list, prefix) {
        for (var i = 0; i < list.length; ++i) {
            var g = list[i];
            var key = _key(prefix, i);
            if (sel[key] === true && g.packages)
                for (var p = 0; p < g.packages.length; ++p)
                    add(g.packages[p]);
            if (g.subgroups)
                walk(g.subgroups, key);
        }
    }
    walk(groups, "");
    return out;
}
