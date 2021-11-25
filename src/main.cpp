#include "definitions.hpp"
#include "utils.hpp"

#include <thread>

static constexpr int32_t SLEEP_TIMEOUT = 15;

int main() {
    if (!utils::is_connected()) {
        warning("It seems you are not connected, waiting for 15s before retrying...\n");
        std::this_thread::sleep_for(std::chrono::seconds(SLEEP_TIMEOUT));

        if (!utils::is_connected()) {
            char type = '\0';

            while (utils::prompt_char("An internet connection could not be detected, do you want to connect to a wifi network? [y/n]", type, RED)) {
                if (type != 'n') {

                    info("\nInstructions to connect to wifi using iwclt:\n");
                    info("1 - Find your wifi device name ex: wlan0\n");
                    info("2 - type `station wlan0 scan`, and wait couple seconds\n");
                    info("3 - type `station wlan0 get-networks` (find your wifi Network name ex. my_wifi)\n");
                    info("4 - type `station wlan0 connect my_wifi` (don't forget to press TAB for auto completion!\n");
                    info("5 - type `station wlan0 show` (status should be connected)\n");
                    info("6 - type `exit`\n");
                    while (utils::prompt_char("Press a key to continue...", type, CYAN)) {
                        utils::exec("iwctl");
                        break;
                    }
                }

                break;
            }
        }
    }
}
