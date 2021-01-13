#!/bin/bash

export DEBEMAIL="lan4lano@gmail.com"
export DEBFULLNAME="Sven Moennich"
export LOGNAME="Sven Moennich"

PROGRAMM=evaftplib

command -v dh_make >/dev/null 2>&1 || { echo >&2 "I require dh_make but it's not installed.  Aborting."; exit 1; }
command -v debuild >/dev/null 2>&1 || { echo >&2 "I require debuild but devscripts it's not installed.  Aborting."; exit 1; }

# Version updaten
CUSTOM_VERSION=$(cat .last_version.number)
echo "Neue Version: ${CUSTOM_VERSION}"


TMPDIR=$(mktemp -d)
SRCDIR=$(pwd)
DSTDIR=$TMPDIR/$PROGRAMM-${CUSTOM_VERSION}
mkdir -p $DSTDIR

cp -r $SRCDIR/* $DSTDIR/
rm -rf $DSTDIR/.git

cd $DSTDIR

dh_make --indep --createorig --copyright gpl --yes

rm debian/*.ex debian/*.EX debian/README.Debian debian/README.source

cat <<EOF >debian/install
evaftplib.h usr/include/
libevaftp.so.4.0 usr/lib/
libevaftp.so.4 usr/lib/
libevaftp.so usr/lib/
EOF

debuild -us -uc

cd ..
cp *.deb $SRCDIR

rm -rf $TMPDIR

err=0
report() {
        err=1
        echo -n "error at line ${BASH_LINENO[0]}, in call to "
        sed -n ${BASH_LINENO[0]}p $0
} >&2
trap report ERR

exit $err
