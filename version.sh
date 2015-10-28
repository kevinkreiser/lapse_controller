#!/bin/bash

#grabs the version string from the header file for use autotools
#which then throws it into a pc file so that pkg-config can use it

if [ ! -f include/lapse_controller.hpp ]; then
	echo "Could not find version information"
	exit 1
fi

major=$(grep -m1 -F VERSION_MAJOR include/lapse_controller.hpp | sed -e "s/.*MAJOR \([0-9]\+\)/\1/g")
minor=$(grep -m1 -F VERSION_MINOR include/lapse_controller.hpp | sed -e "s/.*MINOR \([0-9]\+\)/\1/g")
patch=$(grep -m1 -F VERSION_PATCH include/lapse_controller.hpp | sed -e "s/.*PATCH \([0-9]\+\)/\1/g")

if [ -z $major ] || [ -z $minor ] || [ -z $patch ]; then
	echo "Malformed version information"
	exit 1
fi

printf "%s" "$major.$minor.$patch"
