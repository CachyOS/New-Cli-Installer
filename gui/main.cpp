#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#ifndef COS_BUILD_STATIC
#include "gucc/logger.hpp"
#endif

#include <spdlog/async.h>                     // for create_async
#include <spdlog/common.h>                    // for debug
#include <spdlog/sinks/stdout_color_sinks.h>  // for stdout_color_sink_mt
#include <spdlog/spdlog.h>                    // for set_default_logger, set_level

int main(int argc, char* argv[]) {
    // Initialize logger.
    auto logger = spdlog::create_async<spdlog::sinks::stdout_color_sink_mt>("cachyos_logger");
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("[%r][%^---%L---%$] %v");
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(5));

#ifndef COS_BUILD_STATIC
    // Set gucc logger.
    gucc::logger::set_logger(logger);
#endif

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
