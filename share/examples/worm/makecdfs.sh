#!/bin/sh
#
# $FreeBSD: src/share/examples/worm/makecdfs.sh,v 1.4.6.1 2000/05/25 20:16:09 dirk Exp $
#
# usage: makecdfs "cd title" input-tree output-file "copyright string"
#
# For example:
#
# makecdfs FreeBSD-2.1.5 /a/cdrom-dist /a/cdimage.cd0 "Walnut Creek CDROM \
#	1-510-674-0783  FAX 1-510-674-0821"

if [ "$1" = "-b" ]; then
	bootable="-b floppies/boot.flp -c floppies/boot.catalog"
	shift
else
	bootable=""
fi

if [ $# -lt 4 ]; then
	echo "usage: $0 \"cd-title\" input-tree output-file \"copyright\""
elif [ ! -d $2 ]; then
	echo "$0: $2 is not a directory tree."
else
	title="$1"; shift
	tree=$1; shift
	outfile=$1; shift
	copyright="$*"
	mkisofs $bootable -d -N -D -R -T -V "$title" -P "$copyright" -o $outfile $tree
fi
