#include "tui.hpp"
#include "config.hpp"
#include "definitions.hpp"
#include "screen_service.hpp"
#include "utils.hpp"

/* clang-format off */
#include <algorithm>                               // for transform
#include <memory>                                  // for __shared_ptr_access
#include <string>                                  // for basic_string
#include <ftxui/component/captured_mouse.hpp>      // for ftxui
#include <ftxui/component/component.hpp>           // for Renderer, Button
#include <ftxui/component/component_options.hpp>   // for ButtonOption
#include <ftxui/component/screen_interactive.hpp>  // for Component, ScreenI...
#include <ftxui/dom/elements.hpp>                  // for operator|, size
/* clang-format on */

using namespace ftxui;

namespace tui {

ftxui::Element centered_widget(ftxui::Component& container, const std::string_view& title, const ftxui::Element& widget) {
    return vbox({
        //  -------- Title --------------
        text(title.data()) | bold,
        filler(),
        //  -------- Center Menu --------------
        hbox({
            filler(),
            border(vbox({
                widget,
                separator(),
                container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25),
            })),
            filler(),
        }) | center,
        filler(),
    });
}

ftxui::Component controls_widget(const std::array<std::string_view, 2>&& titles, const std::array<std::function<void()>, 2>&& callbacks, ftxui::ButtonOption* button_option) {
    /* clang-format off */
    auto button_ok       = Button(titles[0].data(), callbacks[0], button_option);
    auto button_quit     = Button(titles[1].data(), callbacks[1], button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_ok,
        Renderer([] { return filler() | size(WIDTH, GREATER_THAN, 3); }),
        button_quit,
    });

    return container;
}

ftxui::Element centered_interative_multi(const std::string_view& title, ftxui::Component& widgets) {
    return vbox({
        //  -------- Title --------------
        text(title.data()) | bold,
        filler(),
        //  -------- Center Menu --------------
        hbox({
            filler(),
            border(vbox({
                widgets->Render(),
            })),
            filler(),
        }) | center,
        filler(),
    });
}

ftxui::Element multiline_text(const std::vector<std::string>& lines) {
    Elements multiline;

    std::transform(lines.cbegin(), lines.cend(), std::back_inserter(multiline),
        [=](const std::string& line) -> Element { return text(line); });
    return vbox(std::move(multiline)) | frame;
}

// BIOS and UEFI
void auto_partition() noexcept {
    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    // Find existing partitions (if any) to remove
    auto parts            = utils::exec(fmt::format("parted -s {} print | {}", config_data["DEVICE"], "awk \'/^ / {print $1}\'"));
    const auto& del_parts = utils::make_multiline(parts);
    for (const auto& del_part : del_parts) {
#ifdef NDEVENV
        utils::exec(fmt::format("parted -s {} rm {}", config_data["DEVICE"], del_part));
#else
        output("{}\n", del_part);
#endif
    }

#ifdef NDEVENV
    // Identify the partition table
    const auto& part_table = utils::exec(fmt::format("parted -s {} print | grep -i \'partition table\' | {}", config_data["DEVICE"], "awk \'{print $3}\'"));

    // Create partition table if one does not already exist
    if ((config_data["SYSTEM"] == "BIOS") && (part_table != "msdos"))
        utils::exec(fmt::format("parted -s {} mklabel msdos", config_data["DEVICE"]));
    if ((config_data["SYSTEM"] == "UEFI") && (part_table != "gpt"))
        utils::exec(fmt::format("parted -s {} mklabel gpt", config_data["DEVICE"]));

    // Create partitions (same basic partitioning scheme for BIOS and UEFI)
    if (config_data["SYSTEM"] == "BIOS")
        utils::exec(fmt::format("parted -s {} mkpart primary ext3 1MiB 513MiB", config_data["DEVICE"]));
    else
        utils::exec(fmt::format("parted -s {} mkpart ESP fat32 1MiB 513MiB", config_data["DEVICE"]));

    utils::exec(fmt::format("parted -s {} set 1 boot on", config_data["DEVICE"]));
    utils::exec(fmt::format("parted -s {} mkpart primary ext3 513MiB 100%", config_data["DEVICE"]));
#endif

    // Show created partitions
    auto disklist = utils::exec(fmt::format("lsblk {} -o NAME,TYPE,FSTYPE,SIZE", config_data["DEVICE"]));

    auto& screen = tui::screen_service::instance()->data();
    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_back     = Button("Back", screen.ExitLoopClosure(), &button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_back,
    });

    auto renderer = Renderer(container, [&] {
        return tui::centered_widget(container, "New CLI Installer", multiline_text(utils::make_multiline(disklist)) | size(HEIGHT, GREATER_THAN, 5));
    });

    screen.Loop(renderer);
}

// Simple code to show devices / partitions.
void show_devices() noexcept {
    auto& screen = tui::screen_service::instance()->data();
    auto lsblk   = utils::exec("lsblk -o NAME,MODEL,TYPE,FSTYPE,SIZE,MOUNTPOINT | grep \"disk\\|part\\|lvm\\|crypt\\|NAME\\|MODEL\\|TYPE\\|FSTYPE\\|SIZE\\|MOUNTPOINT\"");

    /* clang-format off */
    auto button_option   = ButtonOption();
    button_option.border = false;
    auto button_back     = Button("Back", screen.ExitLoopClosure(), &button_option);
    /* clang-format on */

    auto container = Container::Horizontal({
        button_back,
    });

    auto renderer = Renderer(container, [&] {
        return tui::centered_widget(container, "New CLI Installer", multiline_text(utils::make_multiline(lsblk)) | size(HEIGHT, GREATER_THAN, 5));
    });

    screen.Loop(renderer);
}

// This function does not assume that the formatted device is the Root installation device as
// more than one device may be formatted. Root is set in the mount_partitions function.
void select_device() noexcept {
    auto* config_instance    = Config::instance();
    auto& config_data        = config_instance->data();
    auto devices             = utils::exec("lsblk -lno NAME,SIZE,TYPE | grep 'disk' | awk '{print \"/dev/\" $1 \" \" $2}' | sort -u");
    const auto& devices_list = utils::make_multiline(devices);

    auto& screen = tui::screen_service::instance()->data();
    std::int32_t selected{};
    auto menu    = Menu(&devices_list, &selected);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    });

    auto ok_callback = [&] {
        auto src              = devices_list[static_cast<std::size_t>(selected)];
        const auto& lines     = utils::make_multiline(src, " ");
        config_data["DEVICE"] = lines[0];
    };

    ButtonOption button_option{.border = false};
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return tui::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
}

void create_partitions() noexcept {
    static constexpr std::string_view optwipe = "Securely Wipe Device (optional)";
    static constexpr std::string_view optauto = "Automatic Partitioning";

    auto* config_instance = Config::instance();
    auto& config_data     = config_instance->data();

    std::vector<std::string> menu_entries = {
        optwipe.data(),
        optauto.data(),
        "cfdisk",
        "cgdisk",
        "fdisk",
        "gdisk",
        "parted",
    };

    auto& screen = tui::screen_service::instance()->data();
    std::int32_t selected{};
    auto menu    = Menu(&menu_entries, &selected);
    auto content = Renderer(menu, [&] {
        return menu->Render() | center | size(HEIGHT, GREATER_THAN, 10) | size(WIDTH, GREATER_THAN, 40);
    });

    auto ok_callback = [&] {
        const auto& answer = menu_entries[static_cast<std::size_t>(selected)];
        if (answer != optwipe && answer != optauto) {
            utils::exec(fmt::format("{} {}", answer, config_data["DEVICE"]), true);
            return;
        }

        if (answer == optwipe) {
            utils::secure_wipe();
            return;
        }
        if (answer == optauto) {
            auto_partition();
            return;
        }
    };

    ButtonOption button_option{.border = false};
    auto controls_container = controls_widget({"OK", "Cancel"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto controls = Renderer(controls_container, [&] {
        return controls_container->Render() | hcenter | size(HEIGHT, LESS_THAN, 3) | size(WIDTH, GREATER_THAN, 25);
    });

    auto global = Container::Vertical({
        content,
        Renderer([] { return separator(); }),
        controls,
    });

    auto renderer = Renderer(global, [&] {
        return tui::centered_interative_multi("New CLI Installer", global);
    });

    screen.Loop(renderer);
}

void init() noexcept {
    auto& screen     = tui::screen_service::instance()->data();
    auto ok_callback = [=] { info("ok\n"); };
    ButtonOption button_option{.border = false};
    auto container = controls_widget({"OK", "Quit"}, {ok_callback, screen.ExitLoopClosure()}, &button_option);

    auto renderer = Renderer(container, [&] {
        return tui::centered_widget(container, "New CLI Installer", text("TODO!!") | size(HEIGHT, GREATER_THAN, 5));
    });

    screen.Loop(renderer);
}
}  // namespace tui
