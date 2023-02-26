#!/bin/sh
#
# $FreeBSD: src/release/scripts/src-install.sh,v 1.6.6.2 2002/08/08 08:23:53 ru Exp $
#

if [ "`id -u`" != "0" ]; then
	echo "Sorry, this must be done as root."
	exit 1
fi
if [ $# -lt 1 ]; then
	echo "You must specify which components of src to extract"
	echo "possible subcomponents are:"
	echo
	echo "base bin contrib etc games gnu include lib libexec"
	echo "release sbin share sys tools ubin usbin"
	echo
	echo "You may also specify all to extract all subcomponents."
	exit 1
fi

if [ "$1" = "all" ]; then
	dists="base bin contrib etc games gnu include lib libexec release sbin share sys tools ubin usbin"
else
	dists="$*"
fi

echo "Extracting sources into ${DESTDIR}/usr/src..."
for i in $dists; do
	echo "  Extracting source component: $i"
	cat s${i}.?? | tar --unlink -xpzf - -C ${DESTDIR}/usr/src
done
echo "Done extracting sources."
exit 0
