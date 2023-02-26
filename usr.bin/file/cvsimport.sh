#!/bin/sh
# import sequence for file(1)
# This shell script can be used in order to handle future imports
# of newer versions of file(1)
#
# $FreeBSD: src/usr.bin/file/cvsimport.sh,v 1.5 1999/08/28 01:00:56 peter Exp $
if [ $# -ne 2 ] ; then 
	echo "usage: $0 <major> <minor>" 1>&2
	exit 1
fi
version=$1.$2
tar xzf file-$version.tar.gz
cd file-$version
mv file.man file.1
mv magic.man magic.5
rm Magdir/Makefile
mv Magdir/msdos Magdir/ms-dos
cvs -n import src/usr.bin/file DARWIN file_$1_$2
