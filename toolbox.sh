#!/bin/bash
clear

binName="cachyos-installer"

if [[ $EUID -eq 0 ]]; then
	echoRed "You cannot run this as root."
	exit 1
fi

function run() {
	sudo $binName
}

function build() {
	echoGreen "Building CachyOS Installer..."

	if [[ ! -d "cmake-build-release" ]]; then
		mkdir -p cmake-build-release
	fi

	cd cmake-build-release || return
	cmake -D CMAKE_BUILD_TYPE=Release ..
	make -j "$(nproc --all)"
	sudo cp -rf $binName /usr/bin/$binName
	cd ..
}

function buildDebug() {
	echoOrange "Building CachyOS Installer in debug..."

	if [[ ! -d "cmake-build-debug" ]]; then
		mkdir -p cmake-build-debug
	fi

	cd cmake-build-debug || return
	cmake -D CMAKE_BUILD_TYPE=Debug ..
	make -j "$(nproc --all)"
	sudo cp -rf $binName /usr/bin/$binName
	cd ..
}

function pull() {
	git pull
}

function echoOrange() {
	echo -e "\\e[33m$*\\e[0m"
}

function echoRed() {
	echo -e "\\e[31m$*\\e[0m"
}

function echoGreen() {
	echo -e "\\e[32m$*\\e[0m"
}

while [[ $# -gt 0 ]]; do
	keys="$1"
	case $keys in
	-r | --run)
		run
		shift
		;;
	-b | --build)
		build
		shift
		;;
	-bd | --build_debug)
		buildDebug
		shift
		;;
	-p | --pull)
		pull
		shift
		;;
	-h | --help)
		echo "
Toolbox script for CachyOS-Installer
=============================================================================================
| Argument             | Description                                                        |
| -------------------- | -------------------------------------------------------------------|
| -r (--run)           | Run the built binary.                                              |
| -b (--build)         | Build and install.                                                 |
| -bd (--build_debug)  | Build and install as debug.                                        |
| -p (--pull)          | Update from repo.                                                  |
| -h (--help)          | Show help.                                                         |
=============================================================================================

All args are executed in the order they are written in, for
example, \"-p -b -r\" would update the software, then build it, and
then run it.
"
		exit
		;;
	*)
		echo "Unknown arg $1, use -h for help"
		exit
		;;
	esac
done
