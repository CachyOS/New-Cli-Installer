cmake_minimum_required(VERSION 3.16)
project(cachyos-installer-gui
        VERSION 0.0.1
        LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup()

# Suppress QML policy warnings
if(COMMAND qt_policy)
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(cachyos-installer-gui
    main.cpp
    backend/InstallerBackend.cpp
    backend/InstallWorker.cpp
)

qt_add_qml_module(cachyos-installer-gui
    URI CachyInstaller
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CachyInstaller
    QML_FILES
        qml/main.qml
        qml/pages/WelcomePage.qml
        qml/pages/LocalePage.qml
        qml/pages/KeyboardPage.qml
        qml/pages/PartitionPage.qml
        qml/pages/DesktopPage.qml
        qml/pages/PackageChooserPage.qml
        qml/pages/UsersPage.qml
        qml/pages/SummaryPage.qml
        qml/pages/InstallPage.qml
        qml/pages/FinishedPage.qml
        qml/components/WizardNavBar.qml
        qml/components/StepIndicator.qml
    RESOURCES
        assets/desktops/kde.webp
        assets/desktops/gnome.webp
        assets/desktops/cosmic.webp
        assets/desktops/niri.webp
        assets/desktops/budgie.jxl
        assets/desktops/cinnamon.jxl
        assets/desktops/hyprland.jxl
        assets/desktops/i3.jxl
        assets/desktops/lxde.jxl
        assets/desktops/lxqt.jxl
        assets/desktops/mate.jxl
        assets/desktops/openbox.jxl
        assets/desktops/qtile.jxl
        assets/desktops/sway.jxl
        assets/desktops/ukui.jxl
        assets/desktops/wayfire.jxl
        assets/desktops/xfce.jxl
        assets/desktops/no-selection.png
)

# Noctalia Commons module

# Mark all Commons files as singletons and fix resource aliases
set_source_files_properties(noctalia/Commons/Style.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE QT_RESOURCE_ALIAS Style.qml)
set_source_files_properties(noctalia/Commons/Color.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE QT_RESOURCE_ALIAS Color.qml)
set_source_files_properties(noctalia/Commons/Icons.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE QT_RESOURCE_ALIAS Icons.qml)
set_source_files_properties(noctalia/Commons/IconsTabler.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE QT_RESOURCE_ALIAS IconsTabler.qml)

# Fix font resource alias to avoid path doubling
set_source_files_properties(noctalia/Assets/Fonts/tabler/noctalia-tabler-icons.ttf PROPERTIES
    QT_RESOURCE_ALIAS Assets/Fonts/tabler/noctalia-tabler-icons.ttf)

qt_add_qml_module(noctalia-commons
    URI noctalia.Commons
    VERSION 1.0
    STATIC
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/noctalia/Commons
    QML_FILES
        noctalia/Commons/Style.qml
        noctalia/Commons/Color.qml
        noctalia/Commons/Icons.qml
        noctalia/Commons/IconsTabler.qml
    RESOURCES
        noctalia/Assets/Fonts/tabler/noctalia-tabler-icons.ttf
)

# Noctalia Widgets module

# Fix resource aliases for all widget files to avoid path doubling
set(NOCTALIA_WIDGETS
    NButton NText NIcon NLabel NHeader NTextInput NComboBox NCheckbox
    NRadioButton NToggle NBox NScrollView NListView NSlideSwapView
    NLinearGauge NBusyIndicator NCollapsible NDivider NIconButton
    NSearchableComboBox
)
foreach(widget ${NOCTALIA_WIDGETS})
    set_source_files_properties(noctalia/Widgets/${widget}.qml PROPERTIES
        QT_RESOURCE_ALIAS ${widget}.qml)
endforeach()

qt_add_qml_module(noctalia-widgets
    URI noctalia.Widgets
    VERSION 1.0
    STATIC
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/noctalia/Widgets
    QML_FILES
        noctalia/Widgets/NButton.qml
        noctalia/Widgets/NText.qml
        noctalia/Widgets/NIcon.qml
        noctalia/Widgets/NLabel.qml
        noctalia/Widgets/NHeader.qml
        noctalia/Widgets/NTextInput.qml
        noctalia/Widgets/NComboBox.qml
        noctalia/Widgets/NCheckbox.qml
        noctalia/Widgets/NRadioButton.qml
        noctalia/Widgets/NToggle.qml
        noctalia/Widgets/NBox.qml
        noctalia/Widgets/NScrollView.qml
        noctalia/Widgets/NListView.qml
        noctalia/Widgets/NSlideSwapView.qml
        noctalia/Widgets/NLinearGauge.qml
        noctalia/Widgets/NBusyIndicator.qml
        noctalia/Widgets/NCollapsible.qml
        noctalia/Widgets/NDivider.qml
        noctalia/Widgets/NIconButton.qml
        noctalia/Widgets/NSearchableComboBox.qml
    DEPENDENCIES noctalia.Commons
)

target_include_directories(cachyos-installer-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/backend
)

target_link_libraries(cachyos-installer-gui PRIVATE
    cachyos::installer-lib
    Qt6::Quick
    Qt6::QuickControls2
    spdlog::spdlog
    fmt::fmt
    noctalia-commonsplugin
    noctalia-widgetsplugin
)

target_compile_features(cachyos-installer-gui PRIVATE cxx_std_23)
