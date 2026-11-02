SUMMARY = "A date and time library based on C++11/14/17."
AUTHOR = "Howard Hinnant"
HOMEPAGE = "https://github.com/HowardHinnant/date.git"
SECTION = "libs"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=b5d973344b3c7bbf7535f0e6e002d017"

SRC_URI = "git://github.com/HowardHinnant/date.git;branch=master;protocol=https;destsuffix=${BP} \
           "

inherit cmake

PV = "3.0.5"

SRCREV = "179a6b921ccd6179b5b04dce812a89f71a51b4df"

EXTRA_OECMAKE += " \
	-DBUILD_TZ_LIB=ON \
	-DHAS_REMOTE_API=OFF \
	-DUSE_AUTOLOAD=OFF \
	-DUSE_SYSTEM_TZ_DB=ON \
	-DBUILD_SHARED_LIBS=ON \
"
