#include "utils.h"
#include <netdb.h>
#include <string>

bool Utils::isConnected()
{
	return gethostbyname("google.com");
}

string Utils::exec(const string &command)
{
	char buffer[128];
	string result;

	FILE *pipe = popen(command.c_str(), "r");

	if (!pipe) {
		return "popen failed!";
	}

	while (!feof(pipe)) {
		if (fgets(buffer, 128, pipe) != nullptr) {
			result += buffer;
		}
	}

	pclose(pipe);

	return result;
}

bool Utils::promptForChar(const char *prompt, char &read, const char *colour)
{
	std::string tmp;
	std::cout << colour << prompt << std::endl;

	if (std::getline(std::cin, tmp)) {
		if (tmp.length() == 1) {
			read = tmp[0];
		} else {
			read = '\0';
		}

		return true;
	}

	return false;
}
