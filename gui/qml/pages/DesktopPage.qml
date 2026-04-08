import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller
import "../components/Format.js" as Fmt

Item {
    id: root

    property string selectedDesktop: "kde"
    property string selectedKernel: ""
    property bool serverMode: false
    property var excludedPackages: []

    // Default the kernel to the first entry once the backend lists are ready,
    // and warm the net-profiles cache in the background so the advanced package
    // dialog opens instantly the first time (the fetch caches the profile data).
    Connections {
        target: InstallerBackend
        function onListsPopulated() {
            if (root.selectedKernel === "" && InstallerBackend.kernelList.length > 0)
                root.selectedKernel = InstallerBackend.kernelList[0];
            InstallerBackend.fetchDesktopPackages(root.selectedDesktop);
        }
    }

    // Exclusions are per-desktop; drop them when the DE changes so they can't
    // leak onto a different package set.
    onSelectedDesktopChanged: root.excludedPackages = []

    readonly property var desktops: [
        { id: "kde", name: "Plasma Desktop",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/kde.webp",
          description: "Use Plasma to surf the web; keep in touch with colleagues, friends and family; manage your files, enjoy music and videos; and get creative and productive at work. Do it all in a beautiful environment that adapts to your needs, and with the safety, privacy-protection and peace of mind that the best Free Open Source Software has to offer." },
        { id: "gnome", name: "GNOME",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/gnome.webp",
          description: "Get things done with ease, comfort, and control. An easy and elegant way to use your computer, GNOME is designed to help you have the best possible computing experience." },
        { id: "xfce", name: "Xfce4",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/xfce.jxl",
          description: "Xfce is a lightweight desktop environment for UNIX-like operating systems. It aims to be fast and low on system resources, while still being visually appealing and user friendly." },
        { id: "cosmic", name: "Cosmic",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/cosmic.webp",
          description: "COSMIC is a comprehensive operating system GUI environment that features advanced functionality and a responsive design. Its modular architecture is specifically designed to facilitate the creation of unique, branded user experiences with ease." },
        { id: "niri", name: "Niri",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/niri.webp",
          description: "A scrollable-tiling Wayland compositor." },
        { id: "hyprland", name: "Hyprland",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/hyprland.webp",
          description: "Hyprland is a highly customizable dynamic tiling Wayland compositor that doesn't sacrifice on its looks." },
        { id: "cinnamon", name: "Cinnamon",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/cinnamon.jxl",
          description: "Cinnamon is a Linux desktop which provides advanced innovative features and a traditional user experience." },
        { id: "budgie", name: "Budgie",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/budgie.jxl",
          description: "Budgie Desktop is a feature-rich, modern desktop. Budgie's design emphasizes simplicity, minimalism, and elegance." },
        { id: "mate", name: "Mate",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/mate.jxl",
          description: "The MATE Desktop Environment is the continuation of GNOME 2. It provides an intuitive and attractive desktop environment using traditional metaphors for Linux and other Unix-like operating systems." },
        { id: "lxqt", name: "LXQt",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/lxqt.jxl",
          description: "LXQt is a lightweight Qt desktop environment. It will not get in your way. It will not hang or slow down your system. It is focused on being a classic desktop with a modern look and feel." },
        { id: "lxde", name: "LXDE",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/lxde.jxl",
          description: "LXDE is a lightweight and fast desktop environment. It is designed to be user friendly and slim, while keeping the resource usage low." },
        { id: "i3wm", name: "i3 Window Manager",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/i3.jxl",
          description: "i3 is a tiling window manager designed for X11, inspired by wmii and written in C. It supports tiling, stacking, and tabbing layouts, which it handles dynamically." },
        { id: "sway", name: "Sway",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/sway.jxl",
          description: "Sway is a tiling Wayland compositor and a drop-in replacement for the i3 window manager for X11. It works with your existing i3 configuration and supports most of i3's features, plus a few extras." },
        { id: "wayfire", name: "Wayfire",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/wayfire.jxl",
          description: "Wayfire is a wayland compositor based on wlroots. It aims to create a customizable, extendable and lightweight environment without sacrificing its appearance." },
        { id: "bspwm", name: "bspwm",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/no-selection.png",
          description: "bspwm is a tiling window manager that represents windows as the leaves of a full binary tree. bspwm supports multiple monitors and is configured and controlled through messages." },
        { id: "openbox", name: "Openbox",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/openbox.jxl",
          description: "Openbox is a highly configurable, floating window manager with extensive standards support." },
        { id: "qtile", name: "Qtile",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/qtile.jxl",
          description: "Qtile is a X11 window manager that is configured with the Python programming language." },
        { id: "mangowm", name: "MangoWM",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/mangowm.webp",
          description: "MangoWM is a Wayland compositor using Dank Material Shell through Quickshell, with CachyOS-provided dotfiles." }
    ]

    // Keyboard navigation
    focus: true
    Keys.onLeftPressed: carousel.decrementCurrentIndex()
    Keys.onRightPressed: carousel.incrementCurrentIndex()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Style.marginM
        spacing: Style.marginS

        // Title row
        RowLayout {
            Layout.fillWidth: true

            PageTitle {
                Layout.fillWidth: true
                title: "Desktop Environment"
                titleSize: Style.fontSizeXL
            }

            NComboBox {
                Layout.preferredWidth: 220
                label: "Kernel"
                model: Fmt.toComboModel(InstallerBackend.kernelList)
                currentKey: root.selectedKernel
                onSelected: function(key) { root.selectedKernel = key; }
            }

            NCheckbox {
                label: "Server mode (no desktop)"
                checked: root.serverMode
                onToggled: function(val) { root.serverMode = val; }
            }
        }

        // Carousel
        CardCarousel {
            id: carousel
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.serverMode
            model: root.desktops
            titleRole: "name"
            onCurrentIndexChanged: {
                if (currentIndex >= 0 && currentIndex < root.desktops.length)
                    root.selectedDesktop = root.desktops[currentIndex].id;
            }
            trailingComponent: Component {
                NButton {
                    text: "Advanced"
                    outlined: true
                    onClicked: advancedPopup.open()
                }
            }
        }

        // Server mode placeholder
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.serverMode

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Style.marginL

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "No Desktop"
                    font.pixelSize: Style.fontSizeXXL
                    font.weight: Style.fontWeightBold
                    color: Color.mOnSurfaceVariant
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Your system will start up in text-only mode.\nYou can install a desktop environment later."
                    font.pixelSize: Style.fontSizeM
                    color: Color.mOnSurfaceVariant
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }

    // Advanced package dialog
    Popup {
        id: advancedPopup
        anchors.centerIn: parent
        width: Math.min(root.width * 0.7, 600)
        height: Math.min(root.height * 0.75, 500)
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Color.mSurface
            radius: Style.radiusM
            border.color: Color.mOutline
            border.width: 1
        }

        // True while the net-profiles fetch is in flight on the worker thread.
        property bool loading: false

        // Rebuild the list each time it opens so it reflects the current DE.
        onOpened: refreshPackages()

        ListModel { id: pkgModel }

        // Kick off an async net-profiles fetch; the result arrives via the
        // Connections below so the GUI thread never blocks on I/O.
        function refreshPackages() {
            pkgModel.clear();
            advancedPopup.loading = true;
            InstallerBackend.fetchDesktopPackages(root.selectedDesktop);
        }

        Connections {
            target: InstallerBackend
            function onDesktopPackagesReady(desktop, packages) {
                // Ignore stale results for a desktop we're no longer showing.
                if (!advancedPopup.visible || desktop !== root.selectedDesktop)
                    return;
                pkgModel.clear();
                for (var i = 0; i < packages.length; ++i)
                    pkgModel.append({ name: packages[i], checked: root.excludedPackages.indexOf(packages[i]) === -1 });
                advancedPopup.loading = false;
            }
        }

        function setExcluded(name, excluded) {
            var arr = root.excludedPackages.slice();
            var idx = arr.indexOf(name);
            if (excluded && idx === -1)
                arr.push(name);
            else if (!excluded && idx !== -1)
                arr.splice(idx, 1);
            root.excludedPackages = arr;
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Style.marginL
            spacing: Style.marginM

            Text {
                text: "Advanced Package Selection"
                font.pixelSize: Style.fontSizeXL
                font.weight: Style.fontWeightBold
                color: Color.mOnSurface
            }

            Text {
                text: "Uncheck packages you do not want to install with " + (root.desktops[carousel.currentIndex] ? root.desktops[carousel.currentIndex].name : "")
                font.pixelSize: Style.fontSizeM
                color: Color.mOnSurfaceVariant
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Style.radiusS
                color: Color.mSurfaceVariant
                clip: true

                ListView {
                    id: pkgList
                    anchors.fill: parent
                    anchors.margins: Style.marginS
                    model: pkgModel
                    spacing: Style.marginXS
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    delegate: NCheckbox {
                        width: pkgList.width - (pkgList.ScrollBar.vertical.width || 0)
                        label: model.name
                        checked: model.checked
                        onToggled: function(val) {
                            pkgModel.setProperty(index, "checked", val);
                            advancedPopup.setExcluded(model.name, !val);
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: pkgModel.count === 0
                        text: advancedPopup.loading ? "Loading packages…" : "No packages found for this desktop."
                        font.pixelSize: Style.fontSizeM
                        color: Color.mOnSurfaceVariant
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                NButton {
                    text: "Close"
                    onClicked: advancedPopup.close()
                }
            }
        }
    }
}
