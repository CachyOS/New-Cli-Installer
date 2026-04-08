import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import noctalia.Commons
import noctalia.Widgets
import CachyInstaller

Item {
    id: root

    property string selectedDesktop: "kde"
    property bool serverMode: false
    property var excludedPackages: []

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
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/hyprland.jxl",
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
        { id: "ukui", name: "UKUI",
          image: "qrc:/qt/qml/CachyInstaller/assets/desktops/ukui.jxl",
          description: "UKUI is a lightweight desktop environment, which consumes few resources and works with older computers. Its visual appearance is similar to Windows 7, making it easier for new users of Linux." }
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

            Text {
                text: "Desktop Environment"
                font.pixelSize: Style.fontSizeXL
                font.weight: Style.fontWeightBold
                color: Color.mOnSurface
                Layout.fillWidth: true
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

                Text {
                    anchors.centerIn: parent
                    text: "Package list will be populated from net-profiles\nin a future update."
                    font.pixelSize: Style.fontSizeM
                    color: Color.mOnSurfaceVariant
                    horizontalAlignment: Text.AlignHCenter
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
