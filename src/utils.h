#ifndef UTILS_H
#define UTILS_H

#include "definitions.h"
#include <iostream>

using namespace std;


class Utils
{
public:
	static void printBanner();
	static bool isConnected();
	static std::string exec(const string &command);
	static bool promptForChar(const char *prompt, char &read, const char *colour = RESET);

private:
};

#endif//UTILS_H
