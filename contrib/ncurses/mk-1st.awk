# $Id: mk-1st.awk,v 1.45 2000/08/19 19:13:19 tom Exp $
##############################################################################
# Copyright (c) 1998,2000 Free Software Foundation, Inc.                     #
#                                                                            #
# Permission is hereby granted, free of charge, to any person obtaining a    #
# copy of this software and associated documentation files (the "Software"), #
# to deal in the Software without restriction, including without limitation  #
# the rights to use, copy, modify, merge, publish, distribute, distribute    #
# with modifications, sublicense, and/or sell copies of the Software, and to #
# permit persons to whom the Software is furnished to do so, subject to the  #
# following conditions:                                                      #
#                                                                            #
# The above copyright notice and this permission notice shall be included in #
# all copies or substantial portions of the Software.                        #
#                                                                            #
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR #
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   #
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    #
# THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER      #
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING    #
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER        #
# DEALINGS IN THE SOFTWARE.                                                  #
#                                                                            #
# Except as contained in this notice, the name(s) of the above copyright     #
# holders shall not be used in advertising or otherwise to promote the sale, #
# use or other dealings in this Software without prior written               #
# authorization.                                                             #
##############################################################################
#
# Author: Thomas E. Dickey <dickey@clark.net> 1996,1997,2000
#
# Generate list of objects for a given model library
# Variables:
#	name (library name, e.g., "ncurses", "panel", "forms", "menus")
#	model (directory into which we compile, e.g., "obj")
#	prefix (e.g., "lib", for Unix-style libraries)
#	suffix (e.g., "_g.a", for debug libraries)
#	MODEL (e.g., "DEBUG", uppercase; toupper is not portable)
#	depend (optional dependencies for all objects, e.g, ncurses_cfg.h)
#	subset ("none", "base", "base+ext_funcs" or "termlib")
#	target (cross-compile target, if any)
#	ShlibVer ("rel", "abi" or "auto", to augment DoLinks variable)
#	DoLinks ("yes", "reverse" or "no", flag to add symbolic links)
#	rmSoLocs ("yes" or "no", flag to add extra clean target)
#	overwrite ("yes" or "no", flag to add link to libcurses.a
#
# Notes:
#	CLIXs nawk does not like underscores in command-line variable names.
#	Mixed-case is ok.
#	HP/UX requires shared libraries to have executable permissions.
#
function symlink(src,dst) {
		if ( src != dst ) {
			printf "rm -f %s; ", dst
			printf "$(LN_S) %s %s; ", src, dst
		}
	}
function rmlink(directory, dst) {
		printf "\t-rm -f %s/%s\n", directory, dst
}
function removelinks(directory) {
		rmlink(directory, end_name);
		if ( DoLinks == "reverse" ) {
				if ( ShlibVer == "rel" ) {
					rmlink(directory, abi_name);
					rmlink(directory, rel_name);
				} else if ( ShlibVer == "abi" ) {
					rmlink(directory, abi_name);
				}
		} else {
				if ( ShlibVer == "rel" ) {
					rmlink(directory, abi_name);
					rmlink(directory, lib_name);
				} else if ( ShlibVer == "abi" ) {
					rmlink(directory, lib_name);
				}
		}
	}
function sharedlinks(directory) {
		if ( ShlibVer != "auto" ) {
			printf "\tcd %s && (", directory
			if ( DoLinks == "reverse" ) {
				if ( ShlibVer == "rel" ) {
					symlink(lib_name, abi_name);
					symlink(abi_name, rel_name);
				} else if ( ShlibVer == "abi" ) {
					symlink(lib_name, abi_name);
				}
			} else {
				if ( ShlibVer == "rel" ) {
					symlink(rel_name, abi_name);
					symlink(abi_name, lib_name);
				} else if ( ShlibVer == "abi" ) {
					symlink(abi_name, lib_name);
				}
			}
			printf ")\n"
		}
	}
BEGIN	{
		found = 0
		using = 0
	}
	/^@/ {
		using = 0
		if (subset == "none") {
			using = 1
		} else if (index(subset,$2) > 0) {
			if (using == 0) {
				if (found == 0) {
					print  ""
					print  "# generated by mk-1st.awk"
					print  ""
				}
				using = 1
			}
			if ( subset == "termlib") {
				name  = "tinfo"
				OBJS  = MODEL "_T"
			} else {
				OBJS  = MODEL
			}
		}
	}
	/^[@#]/ {
		next
	}
	$1 ~ /trace/ {
		if (traces != "all" && traces != MODEL && $1 != "lib_trace")
			next
	}
	{
		if (using \
		 && ( $2 == "lib" \
		   || $2 == "progs" \
		   || $2 == "c++" \
		   || $2 == "tack" ))
		{
			if ( found == 0 )
			{
				printf "%s_OBJS =", OBJS
				if ( $2 == "lib" )
					found = 1
				else
					found = 2
			}
			printf " \\\n\t../%s/%s.o", model, $1
		}
	}
END	{
		print  ""
		if ( found != 0 )
		{
			printf "\n$(%s_OBJS) : %s\n", OBJS, depend
		}
		if ( found == 1 )
		{
			print  ""
			lib_name = sprintf("%s%s%s", prefix, name, suffix)
			if ( MODEL == "SHARED" )
			{
				abi_name = sprintf("%s.$(ABI_VERSION)", lib_name);
				rel_name = sprintf("%s.$(REL_VERSION)", lib_name);
				if ( DoLinks == "reverse") {
					end_name = lib_name;
				} else {
					if ( ShlibVer == "rel" ) {
						end_name = rel_name;
					} else if ( ShlibVer == "abi" ) {
						end_name = abi_name;
					} else {
						end_name = lib_name;
					}
				}
				printf "../lib/%s : $(%s_OBJS)\n", end_name, OBJS
				print  "\t-@rm -f $@"
				if ( subset == "termlib") {
					printf "\t$(MK_SHARED_LIB) $(%s_OBJS) $(TINFO_LIST)\n", OBJS
				} else {
					printf "\t$(MK_SHARED_LIB) $(%s_OBJS) $(SHLIB_LIST)\n", OBJS
				}
				sharedlinks("../lib")
				print  ""
				print  "install \\"
				print  "install.libs \\"
				printf "install.%s :: $(DESTDIR)$(libdir) ../lib/%s\n", name, end_name
				printf "\t@echo installing ../lib/%s as $(DESTDIR)$(libdir)/%s\n", end_name, end_name
				printf "\t-@rm -f $(DESTDIR)$(libdir)/%s\n", end_name
				printf "\t$(INSTALL_LIB) ../lib/%s $(DESTDIR)$(libdir)/%s\n", end_name, end_name
				sharedlinks("$(DESTDIR)$(libdir)")
				if ( overwrite == "yes" && name == "ncurses" )
				{
					ovr_name = sprintf("libcurses%s", suffix)
					printf "\t@echo linking %s to %s\n", end_name, ovr_name
					printf "\tcd $(DESTDIR)$(libdir) && (rm -f %s; $(LN_S) %s %s; )\n", ovr_name, end_name, ovr_name
				}
				if ( ldconfig != "" ) {
					printf "\t- test -z \"$(DESTDIR)\" && %s\n", ldconfig
				}
				print  ""
				print  "uninstall \\"
				print  "uninstall.libs \\"
				printf "uninstall.%s ::\n", name
				printf "\t@echo uninstalling $(DESTDIR)$(libdir)/%s\n", end_name
				removelinks("$(DESTDIR)$(libdir)")
				if ( overwrite == "yes" && name == "ncurses" )
				{
					ovr_name = sprintf("libcurses%s", suffix)
					printf "\t-@rm -f $(DESTDIR)$(libdir)/%s\n", ovr_name
				}
				if ( rmSoLocs == "yes" ) {
					print  ""
					print  "mostlyclean \\"
					print  "clean ::"
					printf "\t-@rm -f so_locations\n"
				}
			}
			else
			{
				end_name = lib_name;
				printf "../lib/%s : $(%s_OBJS)\n", lib_name, OBJS
				printf "\t$(AR) $(AR_OPTS) $@ $?\n"
				printf "\t$(RANLIB) $@\n"
				if ( target == "vxworks" )
				{
					printf "\t$(LD) $(LD_OPTS) $? -o $(@:.a=.o)\n"
				}
				print  ""
				print  "install \\"
				print  "install.libs \\"
				printf "install.%s :: $(DESTDIR)$(libdir) ../lib/%s\n", name, lib_name
				printf "\t@echo installing ../lib/%s as $(DESTDIR)$(libdir)/%s\n", lib_name, lib_name
				printf "\t$(INSTALL_DATA) ../lib/%s $(DESTDIR)$(libdir)/%s\n", lib_name, lib_name
				if ( overwrite == "yes" && lib_name == "libncurses.a" )
				{
					printf "\t@echo linking libcurses.a to libncurses.a\n"
					printf "\t-@rm -f $(DESTDIR)$(libdir)/libcurses.a\n"
					printf "\t(cd $(DESTDIR)$(libdir) && $(LN_S) libncurses.a libcurses.a)\n"
				}
				printf "\t$(RANLIB) $(DESTDIR)$(libdir)/%s\n", lib_name
				if ( target == "vxworks" )
				{
					printf "\t@echo installing ../lib/lib%s.o as $(DESTDIR)$(libdir)/lib%s.o\n", name, name
					printf "\t$(INSTALL_DATA) ../lib/lib%s.o $(DESTDIR)$(libdir)/lib%s.o\n", name, name
				}
				print  ""
				print  "uninstall \\"
				print  "uninstall.libs \\"
				printf "uninstall.%s ::\n", name
				printf "\t@echo uninstalling $(DESTDIR)$(libdir)/%s\n", lib_name
				printf "\t-@rm -f $(DESTDIR)$(libdir)/%s\n", lib_name
				if ( overwrite == "yes" && lib_name == "libncurses.a" )
				{
					printf "\t@echo linking libcurses.a to libncurses.a\n"
					printf "\t-@rm -f $(DESTDIR)$(libdir)/libcurses.a\n"
				}
				if ( target == "vxworks" )
				{
					printf "\t@echo uninstalling $(DESTDIR)$(libdir)/lib%s.o\n", name
					printf "\t-@rm -f $(DESTDIR)$(libdir)/lib%s.o\n", name
				}
			}
			print ""
			print "clean ::"
			removelinks("../lib");
			print ""
			print "mostlyclean::"
			printf "\t-rm -f $(%s_OBJS)\n", OBJS
		}
		else if ( found == 2 )
		{
			print ""
			print "mostlyclean::"
			printf "\t-rm -f $(%s_OBJS)\n", OBJS
			print ""
			print "clean ::"
			printf "\t-rm -f $(%s_OBJS)\n", OBJS
		}
	}
