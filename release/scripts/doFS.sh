#!/bin/sh
#
# $FreeBSD: src/release/scripts/doFS.sh,v 1.24.2.12 2003/04/04 11:58:05 ru Exp $
#

set -e

export BLOCKSIZE=512

if [ "$1" = "-s" ]; then
	do_size="yes"; shift
else
	do_size=""
fi

FSIMG=$1; shift
RD=$1 ; shift
MNT=$1 ; shift
FSSIZE=$1 ; shift
FSPROTO=$1 ; shift
FSINODE=$1 ; shift
FSLABEL=$1 ; shift

BOOT1="-b ${RD}/trees/bin/boot/boot1"
if [ -f "${RD}/trees/bin/boot/boot2" ]; then
	BOOT2="-s ${RD}/trees/bin/boot/boot2"
else
	BOOT2=""
fi

deadlock=20

dofs_vn () {
    if [ "x$VNDEVICE" = "x" ] ; then
	VNDEVICE=vn0
    fi
    u=`expr $VNDEVICE : 'vn\([0-9]*\)' || true`
    VNDEVICE=vnn$u

    rm -f /dev/*vnn*
    mknod /dev/rvnn${u} c 43 `expr 65538 + $u '*' 8`
    mknod /dev/rvnn${u}c c 43 `expr 2 + $u '*' 8`
    mknod /dev/vnn${u} b 15 `expr 65538 + $u '*' 8`
    mknod /dev/vnn${u}c b 15 `expr 2 + $u '*' 8`

    while true 
    do
	rm -f ${FSIMG}

	umount /dev/${VNDEVICE} 2>/dev/null || true
	umount ${MNT} 2>/dev/null || true
	vnconfig -u /dev/r${VNDEVICE} 2>/dev/null || true

	dd of=${FSIMG} if=/dev/zero count=${FSSIZE} bs=1k 2>/dev/null

	vnconfig -s labels -c /dev/r${VNDEVICE} ${FSIMG}
	disklabel -w -B ${BOOT1} ${BOOT2} ${VNDEVICE} ${FSLABEL}
	newfs -i ${FSINODE} -o space -m 0 /dev/r${VNDEVICE}c

	mount /dev/${VNDEVICE}c ${MNT}

	if [ -d ${FSPROTO} ]; then
		(set -e && cd ${FSPROTO} && find . -print | cpio -dump ${MNT})
	else
		cp -p ${FSPROTO} ${MNT}
	fi

	df -ki ${MNT}

	set `df -ki ${MNT} | tail -1`

	umount ${MNT}
	vnconfig -u /dev/r${VNDEVICE} 2>/dev/null || true

	echo "*** Filesystem is ${FSSIZE} K, $4 left"
	echo "***     ${FSINODE} bytes/inode, $7 left"
	if [ "${do_size}" ]; then
		echo ${FSSIZE} > ${FSIMG}.size
	fi
	break;
    done

    rm -f /dev/*vnn*
}

dofs_md () {
    while true 
    do
	rm -f ${FSIMG}

	if [ "x${MDDEVICE}" != "x" ] ; then
		umount /dev/${MDDEVICE} 2>/dev/null || true
		umount ${MNT} 2>/dev/null || true
		mdconfig -d -u ${MDDEVICE} 2>/dev/null || true
	fi

	dd of=${FSIMG} if=/dev/zero count=${FSSIZE} bs=1k 2>/dev/null

	MDDEVICE=`mdconfig -a -t vnode -f ${FSIMG}`
	if [ ! -c /dev/${MDDEVICE} ] ; then
		if [ -f /dev/MAKEDEV ] ; then
			( cd /dev && sh MAKEDEV ${MDDEVICE} )
		else
			echo "No /dev/$MDDEVICE and no MAKEDEV" 1>&2
			exit 1
		fi
	fi
	disklabel -w -B ${BOOT1} ${BOOT2} ${MDDEVICE} ${FSLABEL}
	newfs -i ${FSINODE} -o space -m 0 /dev/${MDDEVICE}c

	mount /dev/${MDDEVICE}c ${MNT}

	if [ -d ${FSPROTO} ]; then
		(set -e && cd ${FSPROTO} && find . -print | cpio -dump ${MNT})
	else
		cp -p ${FSPROTO} ${MNT}
	fi

	df -ki ${MNT}

	set `df -ki ${MNT} | tail -1`

	umount ${MNT}
	mdconfig -d -u ${MDDEVICE} 2>/dev/null || true

	echo "*** Filesystem is ${FSSIZE} K, $4 left"
	echo "***     ${FSINODE} bytes/inode, $7 left"
	if [ "${do_size}" ]; then
		echo ${FSSIZE} > ${FSIMG}.size
	fi
	break;
    done
}

case `uname -r` in
[1-4].*)
	dofs_vn
	;;
*)
	if [ ! -f "${RD}/trees/bin/boot/boot" ]; then
		cp -p ${RD}/trees/bin/boot/boot1 ${RD}/trees/bin/boot/boot
		if [ -f "${RD}/trees/bin/boot/boot2" ]; then
			cat ${RD}/trees/bin/boot/boot2 >> \
			    ${RD}/trees/bin/boot/boot
		fi
	fi
	BOOT1="-b ${RD}/trees/bin/boot/boot"
	BOOT2=""
	dofs_md
	;;
esac
