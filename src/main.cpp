#include "definitions.h"
#include "utils.h"
#include <atomic>
#include <iostream>

int main()
{
	if (!Utils::isConnected()) {
		warning("It seems you are not connected, waiting for 15s before retrying...");
		sleep(15);

		if (!Utils::isConnected()) {
			char type = '\0';

			while (Utils::promptForChar("An internet connection could not be detected, do you want to connect to a wifi network? [y/n]", type, RED)) {
				if (type != 'n') {

					info("\nInstructions to connect to wifi using iwclt:");
					info("1 - Find your wifi device name ex: wlan0");
					info("2 - type `station wlan0 scan`, and wait couple seconds");
					info("3 - type `station wlan0 get-networks` (find your wifi Network name ex. my_wifi)");
					info("4 - type `station wlan0 connect my_wifi` (don't forget to press TAB for auto completion!");
					info("5 - type `station wlan0 show` (status should be connected)");
					info("6 - type `exit`\n");

					while (Utils::promptForChar("Press a key to continue...", type, CYAN)) {
						Utils::exec("iwctl");
						break;
					}
				}

				break;
			}
		}
	}
}
