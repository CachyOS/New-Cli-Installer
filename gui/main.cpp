#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "InstallerBackend.hpp"

#include "cachyos/logging.hpp"

#include <string>       // for string
#include <string_view>  // for string_view

#include <spdlog/spdlog.h>  // for info

int main(int argc, char* argv[]) {
    // Make gucc log every command it runs ([exec]/[exec_follow] cmd lines), so
    // the GUI matches the headless CLI's verbosity. pkexec strips the launching
    // shell's environment, so we can't rely on the live ISO exporting this.
    qputenv("LOG_EXEC_CMDS", "1");

    // Shared logger: the same single file sink as the CLI/TUI, an stdout echo,
    // and a callback sink that forwards every fully-decorated record to the GUI
    // log view. The GUI therefore shows exactly what a headless CLI install
    // prints (orchestrator messages, command output and errors alike).
    cachyos::installer::logging::init();
    cachyos::installer::logging::attach_stdout_sink();
    cachyos::installer::logging::attach_callback_sink(
        [](std::string_view line) { InstallerBackend::forwardLogLine(std::string{line}); });

    spdlog::info("CachyOS Installer GUI starting...");

    QGuiApplication app(argc, argv);
    app.setApplicationName("CachyOS Installer");
    app.setOrganizationName("CachyOS");

    // Use Basic style to avoid Material/Fusion dependencies
    QQuickStyle::setStyle("Basic");

    QQmlApplicationEngine engine;

    const QUrl url(QStringLiteral("qrc:/qt/qml/CachyInstaller/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.load(url);

    return QGuiApplication::exec();
}
