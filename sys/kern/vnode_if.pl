#!/usr/bin/perl
eval 'exec /usr/bin/perl -S $0 ${1+"$@"}'
    if $running_under_some_shell;

#
# Copyright (c) 1992, 1993
#	The Regents of the University of California.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgement:
#	This product includes software developed by the University of
#	California, Berkeley and its contributors.
# 4. Neither the name of the University nor the names of its contributors
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
#	@(#)vnode_if.sh	8.1 (Berkeley) 6/10/93
# $FreeBSD: src/sys/kern/vnode_if.pl,v 1.21.2.1 2002/01/06 06:46:21 silby Exp $
#
# Script to produce VFS front-end sugar.
#
# usage: vnode_if.pl srcfile
#	(where srcfile is currently /sys/kern/vnode_if.src)
#

my %lockdata;

$cfile = 0;
$hfile = 0;

# Process the command line
#
while ($arg = shift @ARGV) {
    if ($arg eq '-c') {
	$cfile = 1;
    } elsif ($arg eq '-h') {
	$hfile = 1;
    } elsif ($arg eq '-ch' || $arg eq '-hc') {
	$cfile = 1;
	$hfile = 1;
    } elsif ($arg =~ m/\.src$/) {
	$SRC = $arg;
    } else {
	print "usage: vnode_if.pl [-c] [-h] srcfile\n";
	exit(1);
    }
}
if (!$cfile and !$hfile) {
    exit(0);		# nothing asked for..
}

# Names of the created files.
$CFILE='vnode_if.c';
$HEADER='vnode_if.h';

open(SRC,     "<$SRC")   || die "Unable to open input file";

if ($hfile) {
    open(HEADER, ">$HEADER") || die "Unable to create $HEADER";
    # Print out header information for vnode_if.h.
    print HEADER <<'END_OF_LEADING_COMMENT'
/*
 * This file is produced automatically.
 * Do not modify anything in here by hand.
 *
 * Created from $FreeBSD: src/sys/kern/vnode_if.pl,v 1.21.2.1 2002/01/06 06:46:21 silby Exp $
 */

extern struct vnodeop_desc vop_default_desc;
END_OF_LEADING_COMMENT
    ;
}

if ($cfile) {
    open(CFILE,   ">$CFILE") || die "Unable to create $CFILE";
    # Print out header information for vnode_if.c.
    print CFILE <<'END_OF_LEADING_COMMENT'
/*
 * This file is produced automatically.
 * Do not modify anything in here by hand.
 *
 * Created from $FreeBSD: src/sys/kern/vnode_if.pl,v 1.21.2.1 2002/01/06 06:46:21 silby Exp $
 */

#include <sys/param.h>
#include <sys/vnode.h>

struct vnodeop_desc vop_default_desc = {
	1,			/* special case, vop_default => 1 */
	"default",
	0,
	NULL,
	VDESC_NO_OFFSET,
	VDESC_NO_OFFSET,
	VDESC_NO_OFFSET,
	VDESC_NO_OFFSET,
	NULL,
};

END_OF_LEADING_COMMENT
    ;
}

line: while (<SRC>) {
    chop;	# strip record separator
    @Fld = split ' ';
    if (@Fld == 0) {
	next line;
    }
    if (/^#/) {
	if (!/^#%\s+([a-z]+)\s+([a-z]+)\s+(.)\s(.)\s(.)/) {
	    next;
	}
	if (!defined($lockdata{"vop_$1"})) {
	    $lockdata{"vop_$1"} = {};
	}
	$lockdata{"vop_$1"}->{$2} = {
	    'Entry' => $3,
	    'OK'    => $4,
	    'Error' => $5,
	};
	next;
    }

    # Get the function name.
    $name = $Fld[0];
    $uname = uc($name);

    # Get the function arguments.
    for ($numargs = 0; ; ++$numargs) {
	if ($ln = <SRC>) {
	    chomp;
	} else {
	    die "Unable to read through the arguments for \"$name\"";
	}
	if ($ln =~ /^\};/) {
	    last;
	}
        # For the header file
	$a{$numargs} = $ln;

        # The rest of this loop is for the C file
	# Delete comments, if any.
	$ln =~ s/\/\*.*\*\///g;

	# Delete leading/trailing space.
	$ln =~ s/^\s*(.*?)\s*$/$1/;

	# Pick off direction.
	if ($ln =~ s/^INOUT\s+//) {
	    $dir = 'INOUT';
	} elsif ($ln =~ s/^IN\s+//) {
	    $dir = 'IN';
	} elsif ($ln =~ s/^OUT\s+//) {
	    $dir = 'OUT';
	} else {
	     die "No IN/OUT direction for \"$ln\".";
	}
	if ($ln =~ s/^WILLRELE\s+//) {
	    $rele = 'WILLRELE';
	} else {
	    $rele = 'WONTRELE';
	}

	# kill trailing ;
	if ($ln !~ s/;$//) {
	    &bail("Missing end-of-line ; in \"$ln\".");
	}

	# pick off variable name
	if ($ln !~ s/([A-Za-z0-9_]+)$//) {
	    &bail("Missing var name \"a_foo\" in \"$ln\".");
	}
	$arg = $1;

	# what is left must be type
	# (put clean it up some)
	$type = $ln;
	# condense whitespace
	$type =~ s/\s+/ /g;
	$type =~ s/^\s*(.*?)\s*$/$1/;

	$dirs{$numargs} = $dir;
	$reles{$numargs} = $rele;
	$types{$numargs} = $type;
	$args{$numargs} = $arg;
    }

    if ($hfile) {
	# Print out the vop_F_args structure.
	print HEADER "struct ${name}_args {\n\tstruct vnodeop_desc *a_desc;\n";
	for ($c2 = 0; $c2 < $numargs; ++$c2) {
	    $a{$c2} =~ /^\s*(INOUT|OUT|IN)(\s+WILLRELE)?\s+(.*?)\s+(\**)(\S*\;)/;
	    print HEADER "\t$3 $4a_$5\n", 
	}
	print HEADER "};\n";

	# Print out extern declaration.
	print HEADER "extern struct vnodeop_desc ${name}_desc;\n";

	# Print out prototype.
	print HEADER "static __inline int ${uname} __P((\n";
	for ($c2 = 0; $c2 < $numargs; ++$c2) {
	    $a{$c2} =~ /^\s*(INOUT|OUT|IN)(\s+WILLRELE)?\s+(.*?)\s+(\**\S*)\;/;
	    print HEADER "\t$3 $4" .
		($c2 < $numargs-1 ? "," : "));") . "\n";
	}

	# Print out function.
	print HEADER "static __inline int ${uname}(";
	for ($c2 = 0; $c2 < $numargs; ++$c2) {
	    $a{$c2} =~ /\**([^;\s]*)\;[^\s]*$/;
	    print HEADER "$1" . ($c2 < $numargs - 1 ? ', ' : ")\n");
	}
	for ($c2 = 0; $c2 < $numargs; ++$c2) {
	    $a{$c2} =~ /^\s*(INOUT|OUT|IN)(\s+WILLRELE)?\s+(.*?)\s+(\**\S*\;)/;
	    print HEADER "\t$3 $4\n";
	}
	print HEADER "{\n\tstruct ${name}_args a;\n";
	print HEADER "\tint rc;\n";
	print HEADER "\ta.a_desc = VDESC(${name});\n";
	for ($c2 = 0; $c2 < $numargs; ++$c2) {
	    $a{$c2} =~ /(\**)([^;\s]*)([^\s]*)$/;
	    print HEADER "\ta.a_$2 = $2$3\n", 
	}
	for ($c2 = 0; $c2 < $numargs; ++$c2) {
	    if (!exists($args{$c2})) {
		die "Internal error";
	    }
	    if (exists($lockdata{$name}) &&
		exists($lockdata{$name}->{$args{$c2}})) {
		if ($ENV{'DEBUG_ALL_VFS_LOCKS'} =~ /yes/i) {
		    # Add assertions for locking
		    if ($lockdata{$name}->{$args{$c2}}->{Entry} eq "L") {
			print HEADER
			    "\tASSERT_VOP_LOCKED($args{$c2}, \"$uname\");\n";
		    } elsif ($lockdata{$name}->{$args{$c2}}->{Entry} eq "U") {
			print HEADER
			    "\tASSERT_VOP_UNLOCKED($args{$c2}, \"$uname\");\n";
		    } elsif (0) {
			# XXX More checks!
		    }
		}
	    }
	}
	$a{0} =~ /\s\**([^;\s]*);/;
	print HEADER "\trc = VCALL($1, VOFFSET(${name}), &a);\n";
	print HEADER "\treturn (rc);\n";
	print HEADER "}\n";
    }


    if ($cfile) {
	# Print out the vop_F_vp_offsets structure.  This all depends
	# on naming conventions and nothing else.
	printf CFILE "static int %s_vp_offsets[] = {\n", $name;
	# as a side effect, figure out the releflags
	$releflags = '';
	$vpnum = 0;
	for ($i = 0; $i < $numargs; $i++) {
	    if ($types{$i} eq 'struct vnode *') {
		printf CFILE "\tVOPARG_OFFSETOF(struct %s_args,a_%s),\n", 
		$name, $args{$i};
		if ($reles{$i} eq 'WILLRELE') {
		    $releflags = $releflags . '|VDESC_VP' . $vpnum . '_WILLRELE';
		}

		$vpnum++;
	    }
	}

	$releflags =~ s/^\|//;
	print CFILE "\tVDESC_NO_OFFSET\n";
	print CFILE "};\n";

	# Print out the vnodeop_desc structure.
	print CFILE "struct vnodeop_desc ${name}_desc = {\n";
	# offset
	print CFILE "\t0,\n";
	# printable name
	printf CFILE "\t\"%s\",\n", $name;
	# flags
	$vppwillrele = '';
	for ($i = 0; $i < $numargs; $i++) {
	    if ($types{$i} eq 'struct vnode **' && 
		($reles{$i} eq 'WILLRELE')) {
		$vppwillrele = '|VDESC_VPP_WILLRELE';
	    }
	}

	if ($releflags eq '') {
	    printf CFILE "\t0%s,\n", $vppwillrele;
	}
	else {
	    printf CFILE "\t%s%s,\n", $releflags, $vppwillrele;
	}

	# vp offsets
	printf CFILE "\t%s_vp_offsets,\n", $name;
	# vpp (if any)
	printf CFILE "\t%s,\n", &find_arg_with_type('struct vnode **');
	# cred (if any)
	printf CFILE "\t%s,\n", &find_arg_with_type('struct ucred *');
	# proc (if any)
	printf CFILE "\t%s,\n", &find_arg_with_type('struct proc *');
	# componentname
	printf CFILE "\t%s,\n", &find_arg_with_type('struct componentname *');
	# transport layer information
	print CFILE "\tNULL,\n};\n\n";
    }
}
 
if ($hfile) {
    close(HEADER) || die "Unable to close $HEADER";
}
if ($cfile) {
    close(CFILE)  || die "Unable to close $CFILE";
}
close(SRC) || die;

exit 0;

sub find_arg_with_type {
    my $type = shift;
    my $i;

    for ($i=0; $i < $numargs; $i++) {
	if ($types{$i} eq $type) {
	    return "VOPARG_OFFSETOF(struct ${name}_args,a_" . $args{$i} . ")";
	}
    }

    return "VDESC_NO_OFFSET";
}
