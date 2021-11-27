#include "definitions.hpp"
#include "utils.hpp"

#include <regex>
#include <thread>

static constexpr int32_t SLEEP_TIMEOUT = 15;

void show_iwctl() {
    info("\nInstructions to connect to wifi using iwclt:\n");
    info("1 - Find your wifi device name ex: wlan0\n");
    info("2 - type `station wlan0 scan`, and wait couple seconds\n");
    info("3 - type `station wlan0 get-networks` (find your wifi Network name ex. my_wifi)\n");
    info("4 - type `station wlan0 connect my_wifi` (don't forget to press TAB for auto completion!\n");
    info("5 - type `station wlan0 show` (status should be connected)\n");
    info("6 - type `exit`\n");
    while (utils::prompt_char("Press a key to continue...", CYAN)) {
        utils::exec("iwctl");
        break;
    }
}

int main() {
    const auto& tty = utils::exec("tty");
    const std::regex tty_regex("/dev/tty[0-9]*");
    if (std::regex_search(tty, tty_regex)) {
        utils::exec("setterm -blank 0 -powersave off");
    }

#ifdef NDEBUG
    if (!utils::check_root()) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        utils::clear_screen();
        return 1;
    }
#endif

    utils::id_system();
    if (!utils::is_connected()) {
        warning("It seems you are not connected, waiting for 15s before trying again...\n");
        std::this_thread::sleep_for(std::chrono::seconds(SLEEP_TIMEOUT));

        if (!utils::is_connected()) {
            char type = '\0';

            while (utils::prompt_char("An internet connection could not be detected, do you want to connect to a wifi network? [y/n]", RED, &type)) {
                if (type != 'n') {
                    show_iwctl();
                }

                break;
            }
        }
    }
}
