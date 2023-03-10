#!/usr/bin/perl
#
# Copyright (c) 1995-1996 Wolfram Schneider <wosch@FreeBSD.org>. Berlin.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD: src/usr.sbin/adduser/adduser.perl,v 1.44.2.5 2002/08/31 18:29:04 dwmalone Exp $


# read variables
sub variables {
    $verbose = 1;		# verbose = [0-2]
    $usernameregexp = "^[a-z0-9_][a-z0-9_-]*\$"; # configurable
    $defaultusernameregexp = $usernameregexp; # remains constant
    $defaultpasswd = "yes";	# use password for new users
    $dotdir = "/usr/share/skel"; # copy dotfiles from this dir
    $dotdir_bak = $dotdir;
    $send_message = "/etc/adduser.message"; # send message to new user
    $send_message_bak = '/etc/adduser.message';
    $config = "/etc/adduser.conf"; # config file for adduser
    $config_read = 1;		# read config file
    $logfile = "/var/log/adduser"; # logfile
    $home = "/home";		# default HOME
    $etc_shells = "/etc/shells";
    $etc_passwd = "/etc/master.passwd";
    $group = "/etc/group";
    @pwd_mkdb = qw(pwd_mkdb -p); # program for building passwd database


    # List of directories where shells located
    @path = ('/bin', '/usr/bin', '/usr/local/bin');
    # common shells, first element has higher priority
    @shellpref = ('csh', 'sh', 'bash', 'tcsh', 'ksh');

    $defaultshell = 'sh';	# defaultshell if not empty
    $group_uniq = 'USER';
    $defaultgroup = $group_uniq;# login groupname, $group_uniq means username
    $defaultclass = '';

    $uid_start = 1000;		# new users get this uid
    $uid_end   = 32000;		# max. uid

    # global variables
    # passwd
    $username = '';		# $username{username} = uid
    $uid = '';			# $uid{uid} = username
    $pwgid = '';		# $pwgid{pwgid} = username; gid from passwd db

    $password = '';		# password for new users

    # group
    $groupname ='';		# $groupname{groupname} = gid
    $groupmembers = '';		# $groupmembers{gid} = members of group/kommalist
    $gid = '';			# $gid{gid} = groupname;    gid form group db
    @group_comments;		# Comments in the group file

    # shell
    $shell = '';		# $shell{`basename sh`} = sh

    umask 022;			# don't give login group write access

    $ENV{'PATH'} = "/sbin:/bin:/usr/sbin:/usr/bin";
    @passwd_backup = '';
    @group_backup = '';
    @message_buffer = '';
    @user_variable_list = '';	# user variables in /etc/adduser.conf
    $do_not_delete = '## DO NOT DELETE THIS LINE!';
}

# read shell database, see also: shells(5)
sub shells_read {
    local($sh);
    local($err) = 0;

    print "Check $etc_shells\n" if $verbose;
    open(S, $etc_shells) || die "$etc_shells:$!\n";

    while(<S>) {
	if (/^\s*\//) {
	    s/^\s*//; s/\s+.*//; # chop
	    $sh = $_;
	    if (-x  $sh) {
		$shell{&basename($sh)} = $sh;
	    } else {
		warn "Shell: $sh not executable!\n";
		$err++;
	    }
	}
    }

    # Allow /nonexistent and /bin/date as a valid shell for system utils
    push(@list, "/nonexistent");
    push(@shellpref, "no") if !grep(/^no$/, @shellpref);
    $shell{"no"} = "/nonexistent";

    push(@list, "/bin/date");
    push(@shellpref, "date") if !grep(/^date$/, @shellpref);
    $shell{"date"} = "/bin/date";

    return $err;
}

# add new shells if possible
sub shells_add {
    local($sh,$dir,@list);

    return 1 unless $verbose;

    foreach $sh (@shellpref) {
	# all known shells
	if (!$shell{$sh}) {
	    # shell $sh is not defined as login shell
	    foreach $dir (@path) {
		if (-x "$dir/$sh") {
		    # found shell
		    if (&confirm_yn("Found shell: $dir/$sh. Add to $etc_shells?", "yes")) {
			push(@list, "$dir/$sh");
			push(@shellpref, "$sh");
			$shell{&basename("$dir/$sh")} = "$dir/$sh";
			$changes++;
		    }
		}
	    }
	}
    }
    &append_file($etc_shells, @list) if $#list >= 0;
}

# choose your favourite shell and return the shell
sub shell_default {
    local($e,$i,$new_shell);
    local($sh);

    $sh = &shell_default_valid($defaultshell);
    return $sh unless $verbose;

    $new_shell = &confirm_list("Enter your default shell:", 0,
		       $sh, sort(keys %shell));
    print "Your default shell is: $new_shell -> $shell{$new_shell}\n";
    $changes++ if $new_shell ne $sh;
    return $new_shell;
}

sub shell_default_valid {
    local($sh) = @_;
    local($s,$e);

    return $sh if $shell{$sh};

    foreach $e (@shellpref) {
	$s = $e;
	last if defined($shell{$s});
    }
    $s = "sh" unless $s;
    warn "Shell ``$sh'' is undefined, use ``$s''\n";
    return $s;
}

# return default home partition (e.g. "/home")
# create base directory if nesseccary
sub home_partition {
    local($home) = @_;
    $home = &stripdir($home);
    local($h) = $home;

    return $h if !$verbose && $h eq &home_partition_valid($h);

    while(1) {
	$h = &confirm_list("Enter your default HOME partition:", 1, $home, "");
	$h = &stripdir($h);
	last if $h eq &home_partition_valid($h);
    }

    $changes++ if $h ne $home;
    return $h;
}

sub home_partition_valid {
    local($h) = @_;

    $h = &stripdir($h);
    # all right (I hope)
    return $h if $h =~ "^/" && -e $h && -w $h && (-d $h || -l $h);

    # Errors or todo
    if ($h !~ "^/") {
	warn "Please use absolute path for home: ``$h''.\a\n";
	return 0;
    }

    if (-e $h) {
	warn "$h exists, but is not a directory or symlink!\n"
	    unless -d $h || -l $h;
	warn "$h is not writable!\n"
	    unless -w $h;
	return 0;
    } else {
	# create home partition
	return $h if &mkdir_home($h);
    }
    return 0;
}

# check for valid passwddb
sub passwd_check {
    system(@pwd_mkdb, '-C', $etc_passwd);
    die "\nInvalid $etc_passwd - cannot add any users!\n" if $?;
}

# read /etc/passwd
sub passwd_read {
    local($p_username, $pw, $p_uid, $p_gid, $sh, %shlist);

    print "Check $etc_passwd\n" if $verbose;
    open(P, "$etc_passwd") || die "$etc_passwd: $!\n";

    while(<P>) {
	chop;
	push(@passwd_backup, $_);
	# ignore comments
	next if /^\s*$/;
	next if /^\s*#/;

	($p_username, $pw, $p_uid, $p_gid, $sh) = (split(/:/, $_))[0..3,9];

	print "$p_username already exists with uid: $username{$p_username}!\n"
	    if $username{$p_username} && $verbose;
	$username{$p_username} = $p_uid;
	print "User $p_username: uid $p_uid exists twice: $uid{$p_uid}\n"
	    if $uid{$p_uid} && $verbose && $p_uid;    # don't warn for uid 0
	print "User $p_username: illegal shell: ``$sh''\n"
	    if ($verbose && $sh &&
		!$shell{&basename($sh)} &&
		$p_username !~ /^(news|xten|bin|nobody|uucp)$/ &&
		$sh !~ /\/(pppd|sliplogin|nologin|nonexistent)$/);
	$uid{$p_uid} = $p_username;
	$pwgid{$p_gid} = $p_username;
    }
    close P;
}

# read /etc/group
sub group_read {
    local($g_groupname,$pw,$g_gid, $memb);

    print "Check $group\n" if $verbose;
    open(G, "$group") || die "$group: $!\n";
    while(<G>) {
	chop;
	push(@group_backup, $_);
	# Ignore empty lines
	next if /^\s*$/;
	# Save comments to restore later
	if (/^\s*\#/) {
	    push(@group_comments, $_);
	    next;
	}

	($g_groupname, $pw, $g_gid, $memb) = (split(/:/, $_))[0..3];

	$groupmembers{$g_gid} = $memb;
	warn "Groupname exists twice: $g_groupname:$g_gid ->  $g_groupname:$groupname{$g_groupname}\n"
	    if $groupname{$g_groupname} && $verbose;
	$groupname{$g_groupname} = $g_gid;
	warn "Groupid exists twice:   $g_groupname:$g_gid -> $gid{$g_gid}:$g_gid\n"
	    if $gid{$g_gid} && $verbose;
	$gid{$g_gid} = $g_groupname;
    }
    close G;
}

# check gids /etc/passwd <-> /etc/group
sub group_check {
    local($c_gid, $c_username, @list);

    foreach $c_gid (keys %pwgid) {
	if (!$gid{$c_gid}) {
	    $c_username = $pwgid{$c_gid};
	    warn "User ``$c_username'' has gid $c_gid but a group with this " .
		"gid does not exist.\n" if $verbose;
	}
    }
}

#
# main loop for creating new users
#

# return username
sub new_users_name {
    local($name);

    while(1) {
	$name = &confirm_list("Enter username", 1, $usernameregexp, "");
	last if (&new_users_name_valid($name));
    }
    return $name;
}

sub new_users_name_valid {
    local($name) = @_;

    if ($name eq $usernameregexp) { # user/admin just pressed <Return>
	warn "Please enter a username\a\n";
	return 0;
    } elsif (length($name) > 16) {
	warn "Username is longer than 16 characters.\a\n";
	return 0;
    } elsif ($name =~ /[:\n]/) {
	warn "Username cannot contain colon or newline characters.\a\n";
	return 0;
    } elsif ($name !~ /$usernameregexp/) {
	if ($usernameregexp eq $defaultusernameregexp) {
	    warn "Illegal username.\n" .
		"Please use only lowercase Roman, decimal, underscore, " .
		"or hyphen characters.\n" .
		"Additionally, a username should not start with a hyphen.\a\n";
	} else {
	    warn "Username doesn't match the regexp /$usernameregexp/\a\n";
	}
	return 0;
    } elsif (defined($username{$name})) {
	warn "Username ``$name'' already exists!\a\n"; return 0;
    }
    return 1;
}

# return full name
sub new_users_fullname {
    local($name) = @_;
    local($fullname);

    while(1) {
	$fullname = &confirm_list("Enter full name", 1, "", "");
	last if $fullname eq &new_users_fullname_valid($fullname);
    }
    $fullname = $name unless $fullname;
    return $fullname;
}

sub new_users_fullname_valid {
    local($fullname) = @_;

    return $fullname if $fullname !~ /:/;

    warn "``:'' is not allowed!\a\n";
    return 0;
}

# return shell (full path) for user
sub new_users_shell {
    local($sh);

    $sh = &confirm_list("Enter shell", 0, $defaultshell, keys %shell);
    return $shell{$sh};
}

# return home (full path) for user
# Note that the home path defaults to $home/$name for batch
sub new_users_home {
    local($name) = @_;
    local($userhome);

    while(1) {
        $userhome = &confirm_list("Enter home directory (full path)", 1, "$home/$name", "");
	last if $userhome =~ /^\//;
	warn qq{Home directory "$userhome" is not a full path\a\n};
    }
    return $userhome;
}

# return free uid and gid
sub new_users_id {
    local($name) = @_;
    local($u_id, $g_id) = &next_id($name);
    local($u_id_tmp, $e);

    while(1) {
	$u_id_tmp = &confirm_list("Uid", 1, $u_id, "");
	last if $u_id_tmp =~ /^[0-9]+$/ && $u_id_tmp <= $uid_end &&
		! $uid{$u_id_tmp};
	if ($uid{$u_id_tmp}) {
	    warn "Uid ``$u_id_tmp'' in use!\a\n";
	    $uid_start = $u_id_tmp;
	    ($u_id, $g_id) = &next_id($name);
	    next;
	} else {
	    warn "Wrong uid.\a\n";
	}
    }
    # use calculated uid
    # return ($u_id_tmp, $g_id) if $u_id_tmp eq $u_id;
    # recalculate gid
    $uid_start = $u_id_tmp;
    return &next_id($name);
}

# return login class for user
sub new_users_class {
    local($def) = @_;
    local($class);

    $class = &confirm_list("Enter login class:", 1, $def, ($def, "default"));
    $class = "" if $class eq "default";
    return $class;
}

# add user to group
sub add_group {
    local($gid, $name) = @_;

    return 0 if
	$groupmembers{$gid} =~ /^(.+,)?$name(,.+)?$/;

    $groupmembers_bak{$gid} = $groupmembers{$gid};
    $groupmembers{$gid} .= "," if $groupmembers{$gid};
    $groupmembers{$gid} .= "$name";

    return $name;
}


# return login group
sub new_users_grplogin {
    local($name, $defaultgroup, $new_users_ok) = @_;
    local($group_login, $group);

    $group = $name;
    $group = $defaultgroup if $defaultgroup ne $group_uniq;

    if ($new_users_ok) {
	# clean up backup
	foreach $e (keys %groupmembers_bak) { delete $groupmembers_bak{$e}; }
    } else {
	# restore old groupmembers, user was not accept
	foreach $e (keys %groupmembers_bak) {
	    $groupmembers{$e} = $groupmembers_bak{$e};
	}
    }

    while(1) {
	$group_login = &confirm_list("Login group", 1, $group,
				     ($name, $group));
	last if $group_login eq $group;
	last if $group_login eq $name;
	last if defined $groupname{$group_login};
	if ($group_login eq $group_uniq) {
	    $group_login = $name; last;
	}

	if (defined $gid{$group_login}) {
	    # convert numeric groupname (gid) to groupname
	    $group_login = $gid{$group_login};
	    last;
	}
	warn "Group does not exist!\a\n";
    }

    #if (defined($groupname{$group_login})) {
    #	&add_group($groupname{$group_login}, $name);
    #}

    return ($group_login, $group_uniq) if $group_login eq $name;
    return ($group_login, $group_login);
}

# return other groups (string)
sub new_users_groups {
    local($name, $other_groups) = @_;
    local($string) =
	"Login group is ``$group_login''. Invite $name into other groups:";
    local($e, $flag);
    local($new_groups,$groups);

    $other_groups = "no" unless $other_groups;

    while(1) {
	$groups = &confirm_list($string, 1, $other_groups,
				("no", $other_groups, "guest"));
	# no other groups
	return "" if $groups eq "no";

	($flag, $new_groups) = &new_users_groups_valid($groups);
	last unless $flag;
    }
    $new_groups =~ s/\s*$//;
    return $new_groups;
}

sub new_users_groups_valid {
    local($groups) = @_;
    local($e, $new_groups);
    local($flag) = 0;

    foreach $e (split(/[,\s]+/, $groups)) {
	# convert numbers to groupname
	if ($e =~ /^[0-9]+$/ && $gid{$e}) {
	    $e = $gid{$e};
	}
	if (defined($groupname{$e})) {
	    if ($e eq $group_login) {
		# do not add user to a group if this group
		# is also the login group.
	    } elsif (&add_group($groupname{$e}, $name)) {
		$new_groups .= "$e ";
	    } else {
		warn "$name is already member of group ``$e''\n";
	    }
	} else {
	    warn "Group ``$e'' does not exist\a\n"; $flag++;
	}
    }
    return ($flag, $new_groups);
}

# your last change
sub new_users_ok {

    print <<EOF;

Name:	  $name
Password: ****
Fullname: $fullname
Uid:	  $u_id
Gid:	  $g_id ($group_login)
Class:	  $class
Groups:	  $group_login $new_groups
HOME:     $userhome
Shell:	  $sh
EOF

    return &confirm_yn("OK?", "yes");
}

# make password database
sub new_users_pwdmkdb {
    local($last) = shift;
    local($name) = shift;

    system(@pwd_mkdb, '-u', $name, $etc_passwd);
    if ($?) {
	warn "$last\n";
	warn "``@pwd_mkdb'' failed\n";
	exit($? >> 8);
    }
}

# update group database
sub new_users_group_update {
    local($e, @a);

    # Add *new* group
    if (!defined($groupname{$group_login}) &&
	!defined($gid{$groupname{$group_login}})) {
	push(@group_backup, "$group_login:*:$g_id:");
	$groupname{$group_login} = $g_id;
	$gid{$g_id} = $group_login;
	# $groupmembers{$g_id} = $group_login;
    }

    if ($new_groups || defined($groupname{$group_login}) ||
	defined($gid{$groupname{$group_login}}) &&
		$gid{$groupname{$group_login}} ne "+") {
	# new user is member of some groups
	# new login group is already in name space
	rename($group, "$group.bak");
	#warn "$group_login $groupname{$group_login} $groupmembers{$groupname{$group_login}}\n";

	# Restore comments from the top of the group file
	@a = @group_comments;
	foreach $e (sort {$a <=> $b} (keys %gid)) {
	    push(@a, "$gid{$e}:*:$e:$groupmembers{$e}");
	}
	&append_file($group, @a);
    } else {
	&append_file($group, "$group_login:*:$g_id:");
    }

}

sub new_users_passwd_update {
    # update passwd/group variables
    push(@passwd_backup, $new_entry);
    $username{$name} = $u_id;
    $uid{$u_id} = $name;
    $pwgid{$g_id} = $name;
}

# send message to new user
sub new_users_sendmessage {
    return 1 if $send_message eq "no";

    local($cc) =
	&confirm_list("Send message to ``$name'' and:",
		      1, "no", ("root", "second_mail_address", "no"));
    local($e);
    $cc = "" if $cc eq "no";

    foreach $e (@message_buffer) {
	print eval "\"$e\"";
    }
    print "\n";

    local(@message_buffer_append) = ();
    if (!&confirm_yn("Add anything to default message", "no")) {
	print "Use ``.'' or ^D alone on a line to finish your message.\n";
	push(@message_buffer_append, "\n");
	while($read = <STDIN>) {
	    last if $read eq "\.\n";
	    push(@message_buffer_append, $read);
	}
    }

    &sendmessage("$name $cc", (@message_buffer, @message_buffer_append))
	if (&confirm_yn("Send message", "yes"));
}

sub sendmessage {
    local($to, @message) = @_;
    local($e);

    if (!open(M, "| mail -s Welcome $to")) {
	warn "Cannot send mail to: $to!\n";
	return 0;
    } else {
	foreach $e (@message) {
	    print M eval "\"$e\"";
	}
	close M;
    }
}


sub new_users_password {

    # empty password
    return "" if $defaultpasswd ne "yes";

    local($password);

    while(1) {
	system("stty -echo");
	$password = &confirm_list("Enter password", 1, "", "");
	system("stty echo");
	print "\n";
	if ($password ne "") {
	    system("stty -echo");
	    $newpass = &confirm_list("Enter password again", 1, "", "");
	    system("stty echo");
	    print "\n";
	    last if $password eq $newpass;
	    print "They didn't match, please try again\n";
	}
	elsif (&confirm_yn("Use an empty password?", "yes")) {
	    last;
	}
    }

    return $password;
}


sub new_users {

    print "\n" if $verbose;
    print "Ok, let's go.\n" .
	  "Don't worry about mistakes. I will give you the chance later to " .
	  "correct any input.\n" if $verbose;

    # name: Username
    # fullname: Full name
    # sh: shell
    # userhome: home path for user
    # u_id: user id
    # g_id: group id
    # class: login class
    # group_login: groupname of g_id
    # new_groups: some other groups
    local($name, $group_login, $fullname, $sh, $u_id, $g_id, $class, $new_groups);
    local($userhome);
    local($groupmembers_bak, $cryptpwd);
    local($new_users_ok) = 1;


    $new_groups = "no";
    $new_groups = "no" unless $groupname{$new_groups};

    while(1) {
	$name = &new_users_name;
	$fullname = &new_users_fullname($name);
	$sh = &new_users_shell;
        $userhome = &new_users_home($name);
	($u_id, $g_id) = &new_users_id($name);
	$class = &new_users_class($defaultclass);
	($group_login, $defaultgroup) =
	    &new_users_grplogin($name, $defaultgroup, $new_users_ok);
	# do not use uniq username and login group
	$g_id = $groupname{$group_login} if (defined($groupname{$group_login}));

	$new_groups = &new_users_groups($name, $new_groups);
	$password = &new_users_password;


	if (&new_users_ok) {
	    $new_users_ok = 1;

	    $cryptpwd = "";
	    $cryptpwd = crypt($password, &salt) if $password ne "";
	    # obscure perl bug
	    $new_entry = "$name\:" . "$cryptpwd" .
		"\:$u_id\:$g_id\:$class\:0:0:$fullname:$userhome:$sh";
	    &append_file($etc_passwd, "$new_entry");
	    &new_users_pwdmkdb("$new_entry", $name);
	    &new_users_group_update;
	    &new_users_passwd_update;  print "Added user ``$name''\n";
	    &new_users_sendmessage;
	    &adduser_log("$name:*:$u_id:$g_id($group_login):$fullname");
	    &home_create($userhome, $name, $group_login);
	} else {
	    $new_users_ok = 0;
	}
	if (!&confirm_yn("Add another user?", "yes")) {
	    print "Goodbye!\n" if $verbose;
	    last;
	}
	print "\n" if !$verbose;
    }
}

# ask for password usage
sub password_default {
    local($p) = $defaultpasswd;
    if ($verbose) {
	$p = &confirm_yn("Use passwords", $defaultpasswd);
	$changes++ unless $p;
    }
    return "yes" if (($defaultpasswd eq "yes" && $p) ||
		     ($defaultpasswd eq "no" && !$p));
    return "no";    # otherwise
}

# misc
sub check_root {
    die "You are not root!\n" if $< && !$test;
}

sub usage {
    warn <<USAGE;
usage: adduser
    [-check_only]
    [-class login_class]
    [-config_create]
    [-dotdir dotdir]
    [-group login_group]
    [-h|-help]
    [-home home]
    [-message message_file]
    [-noconfig]
    [-shell shell]
    [-s|-silent|-q|-quiet]
    [-uid uid_start]
    [-v|-verbose]

home=$home shell=$defaultshell dotdir=$dotdir login_group=$defaultgroup
login_class=$defaultclass message_file=$send_message uid_start=$uid_start
USAGE
    exit 1;
}

# uniq(1)
sub uniq {
    local(@list) = @_;
    local($e, $last, @array);

    foreach $e (sort @list) {
	push(@array, $e) unless $e eq $last;
	$last = $e;
    }
    return @array;
}

# see /usr/src/usr.bin/passwd/local_passwd.c or librcypt, crypt(3)
sub salt {
    local($salt);		# initialization
    local($i, $rand);
    local(@itoa64) = ( '0' .. '9', 'a' .. 'z', 'A' .. 'Z' ); # 0 .. 63

    warn "calculate salt\n" if $verbose > 1;
    # to64
    for ($i = 0; $i < 27; $i++) {
	srand(time + $rand + $$); 
	$rand = rand(25*29*17 + $rand);
	$salt .=  $itoa64[$rand & $#itoa64];
    }
    warn "Salt is: $salt\n" if $verbose > 1;

    return $salt;
}


# print banner
sub copyright {
    return;
}

# hints
sub hints {
    if ($verbose) {
	print "Use option ``-silent'' if you don't want to see " .
	      "all warnings and questions.\n\n";
    } else {
	print "Use option ``-verbose'' if you want to see more warnings and " .
	      "questions \nor try to repair bugs.\n\n";
    }
}

#
sub parse_arguments {
    local(@argv) = @_;

    while ($_ = $argv[0], /^-/) {
	shift @argv;
	last if /^--$/;
	if    (/^--?(v|verbose)$/)	{ $verbose = 1 }
	elsif (/^--?(s|silent|q|quiet)$/)  { $verbose = 0 }
	elsif (/^--?(debug)$/)	    { $verbose = 2 }
	elsif (/^--?(h|help|\?)$/)	{ &usage }
	elsif (/^--?(home)$/)	 { $home = $argv[0]; shift @argv }
	elsif (/^--?(shell)$/)	 { $defaultshell = $argv[0]; shift @argv }
	elsif (/^--?(dotdir)$/)	 { $dotdir = $argv[0]; shift @argv }
	elsif (/^--?(uid)$/)	 { $uid_start = $argv[0]; shift @argv }
	elsif (/^--?(class)$/)   { $defaultclass = $argv[0]; shift @argv }
	elsif (/^--?(group)$/)	 { $defaultgroup = $argv[0]; shift @argv }
	elsif (/^--?(check_only)$/) { $check_only = 1 }
	elsif (/^--?(message)$/) { $send_message = $argv[0]; shift @argv;
				   $sendmessage = 1; }
	elsif (/^--?(batch)$/)	 {
	    warn "The -batch option is not supported anymore.\n",
	    "Please use the pw(8) command line tool!\n";
	    exit(0);
	}
	# see &config_read
	elsif (/^--?(config_create)$/)	{ &create_conf; }
	elsif (/^--?(noconfig)$/)	{ $config_read = 0; }
	else			    { &usage }
    }
    #&usage if $#argv < 0;
}

sub basename {
    local($name) = @_;
    $name =~ s|/+$||;
    $name =~ s|.*/+||;
    return $name;
}

sub dirname {
    local($name) = @_;
    $name = &stripdir($name);
    $name =~ s|/+[^/]+$||;
    $name = "/" unless $name;	# dirname of / is /
    return $name;
}

# return 1 if $file is a readable file or link
sub filetest {
    local($file, $verb) = @_;

    if (-e $file) {
	if (-f $file || -l $file) {
	    return 1 if -r _;
	    warn "$file unreadable\n" if $verbose;
	} else {
	    warn "$file is not a plain file or link\n" if $verbose;
	}
    }
    return 0;
}

# create configuration files and exit
sub create_conf {
    $create_conf = 1;
    if ($send_message ne 'no') {
	&message_create($send_message);
    } else {
	&message_create($send_message_bak);
    }
    &config_write(1);
    exit(0);
}

# log for new user in /var/log/adduser
sub adduser_log {
    local($string) = @_;
    local($e);

    return 1 if $logfile eq "no";

    local($sec, $min, $hour, $mday, $mon, $year) = localtime;
    $year += 1900;
    $mon++;

    foreach $e ('sec', 'min', 'hour', 'mday', 'mon', 'year') {
	# '7' -> '07'
	eval "\$$e = 0 . \$$e" if (eval "\$$e" < 10);
    }

    &append_file($logfile, "$year/$mon/$mday $hour:$min:$sec $string");
}

# create HOME directory, copy dotfiles from $dotdir to $HOME
sub home_create {
    local($homedir, $name, $group) = @_;
    local($rootdir);

    if (-e "$homedir") {
	warn "HOME directory ``$homedir'' already exists.\a\n";
	return 0;
    }

    # if the home directory prefix doesn't exist, create it
    # First, split the directory into a list; then remove the user's dir
    @dir = split('/', $homedir); pop(@dir);
    # Put back together & strip to get directory prefix
    $rootdir = &stripdir(join('/', @dir));

    if (!&mkdirhier("$rootdir")) {
	    # warn already displayed
	    return 0;
    }

    if ($dotdir eq 'no') {
	if (!mkdir("$homedir", 0755)) {
	    warn "$dir: $!\n"; return 0;
	}
	system 'chown', "$name:$group", $homedir;
	return !$?;
    }

    # copy files from  $dotdir to $homedir
    # rename 'dot.foo' files to '.foo'
    print "Copy files from $dotdir to $homedir\n" if $verbose;
    system('cp', '-R', $dotdir, $homedir);
    system('chmod', '-R', 'u+wrX,go-w', $homedir);
    system('chown', '-Rh', "$name:$group", $homedir);

    # security
    opendir(D, $homedir);
    foreach $file (readdir(D)) {
	if ($file =~ /^dot\./ && -f "$homedir/$file") {
	    $file =~ s/^dot\././;
	    rename("$homedir/dot$file", "$homedir/$file");
	}
	chmod(0600, "$homedir/$file")
	    if ($file =~ /^\.(rhosts|Xauthority|kermrc|netrc)$/);
	chmod(0700, "$homedir/$file")
	    if ($file =~ /^(Mail|prv|\.(iscreen|term))$/);
    }
    closedir D;
    return 1;
}

# makes a directory hierarchy
sub mkdir_home {
    local($dir) = @_;
    $dir = &stripdir($dir);
    local($user_partition) = "/usr";
    local($dirname) = &dirname($dir);

    -e $dirname || &mkdirhier($dirname);

    if (((stat($dirname))[0]) == ((stat("/"))[0])){
	# home partition is on root partition
	# create home partition on $user_partition and make
	# a symlink from $dir to $user_partition/`basename $dir`
	# For instance: /home -> /usr/home

	local($basename) = &basename($dir);
	local($d) = "$user_partition/$basename";


	if (-d $d) {
	    warn "Oops, $d already exists.\n" if $verbose;
	} else {
	    print "Create $d\n" if $verbose;
	    if (!mkdir("$d", 0755)) {
		warn "$d: $!\a\n"; return 0;
	    }
	}

	unlink($dir);		# symlink to nonexist file
	print "Create symlink: $dir -> $d\n" if $verbose;
	if (!symlink("$d", $dir)) {
	    warn "Symlink $d: $!\a\n"; return 0;
	}
    } else {
	print "Create $dir\n" if $verbose;
	if (!mkdir("$dir", 0755)) {
	    warn "Directory ``$dir'': $!\a\n"; return 0;
	}
    }
    return 1;
}

sub mkdirhier {
    local($dir) = @_;
    local($d,$p);

    $dir = &stripdir($dir);

    foreach $d (split('/', $dir)) {
	$dir = "$p/$d";
	$dir =~ s|^//|/|;
	if (! -e "$dir") {
	    print "Create $dir\n" if $verbose;
	    if (!mkdir("$dir", 0755)) {
		warn "$dir: $!\n"; return 0;
	    }
	}
	$p .= "/$d";
    }
    return 1;
}

# stript unused '/'
# F.i.: //usr///home// -> /usr/home
sub stripdir {
    local($dir) = @_;

    $dir =~ s|/+|/|g;		# delete double '/'
    $dir =~ s|/$||;		# delete '/' at end
    return $dir if $dir ne "";
    return '/';
}

# Read one of the elements from @list. $confirm is default.
# If !$allow accept only elements from @list.
sub confirm_list {
    local($message, $allow, $confirm, @list) = @_;
    local($read, $c, $print);

    $print = "$message" if $message;
    $print .= " " unless $message =~ /\n$/ || $#list == 0;

    $print .= join($", &uniq(@list)); #"
    $print .= " " unless $message =~ /\n$/ && $#list == 0;
    print "$print";
    print "\n" if (length($print) + length($confirm)) > 60;
    print "[$confirm]: ";

    chop($read = <STDIN>);
    $read =~ s/^\s*//;
    $read =~ s/\s*$//;
    return $confirm if $read eq "";
    return "$read" if $allow;

    foreach $c (@list) {
	return $read if $c eq $read;
    }
    warn "$read: is not allowed!\a\n";
    return &confirm_list($message, $allow, $confirm, @list);
}

# YES or NO question
# return 1 if &confirm("message", "yes") and answer is yes
#	or if &confirm("message", "no") an answer is no
# otherwise 0
sub confirm_yn {
    local($message, $confirm) = @_;
    local($yes) = '^(yes|YES|y|Y)$';
    local($no) = '^(no|NO|n|N)$';
    local($read, $c);

    if ($confirm && ($confirm =~ "$yes" || $confirm == 1)) {
	$confirm = "y";
    } else {
	$confirm = "n";
    }
    print "$message (y/n) [$confirm]: ";
    chop($read = <STDIN>);
    $read =~ s/^\s*//;
    $read =~ s/\s*$//;
    return 1 unless $read;

    if (($confirm eq "y" && $read =~ "$yes") ||
	($confirm eq "n" && $read =~ "$no")) {
	return 1;
    }

    if ($read !~ "$yes" && $read !~ "$no") {
	warn "Wrong value. Enter again!\a\n";
	return &confirm_yn($message, $confirm);
    }
    return 0;
}

# allow configuring usernameregexp
sub usernameregexp_default {
    local($r) = $usernameregexp;

    while ($verbose) {
	$r = &confirm_list("Usernames must match regular expression:", 1,
	    $r, "");
	eval "'foo' =~ /$r/";
	last unless $@;
	warn "Invalid regular expression\a\n";
    }
    $changes++ if $r ne $usernameregexp;
    return $r;
}

# test if $dotdir exist
# return "no" if $dotdir not exist or dotfiles should not copied
sub dotdir_default {
    local($dir) = $dotdir;

    return &dotdir_default_valid($dir) unless $verbose;
    while($verbose) {
	$dir = &confirm_list("Copy dotfiles from:", 1,
	    $dir, ("no", $dotdir_bak, $dir));
	last if $dir eq &dotdir_default_valid($dir);
    }
    warn "Do not copy dotfiles.\n" if $verbose && $dir eq "no";

    $changes++ if $dir ne $dotdir;
    return $dir;
}

sub dotdir_default_valid {
    local($dir) = @_;

    return $dir if (-e $dir && -r _ && (-d _ || -l $dir) && $dir =~ "^/");
    return $dir if $dir eq "no";
    warn "Dotdir ``$dir'' is not a directory\a\n";
    return "no";
}

# ask for messages to new users
sub message_default {
    local($file) = $send_message;
    local(@d) = ($file, $send_message_bak, "no");

    while($verbose) {
	$file = &confirm_list("Send message from file:", 1, $file, @d);
	last if $file eq "no";
	last if &filetest($file, 1);

	# maybe create message file
	&message_create($file) if &confirm_yn("Create ``$file''?", "yes");
	last if &filetest($file, 0);
	last if !&confirm_yn("File ``$file'' does not exist, try again?",
			     "yes");
    }

    if ($file eq "no" || !&filetest($file, 0)) {
	warn "Do not send message\n" if $verbose;
	$file = "no";
    } else {
	&message_read($file);
    }

    $changes++ if $file ne $send_message && $verbose;
    return $file;
}

# create message file
sub message_create {
    local($file) = @_;

    rename($file, "$file.bak");
    if (!open(M, "> $file")) {
	warn "Messagefile ``$file'': $!\n"; return 0;
    }
    print M <<EOF;
#
# Message file for adduser(8)
#   comment: ``#''
#   default variables: \$name, \$fullname, \$password
#   other variables:  see /etc/adduser.conf after
#		     line  ``$do_not_delete''
#

\$fullname,

your account ``\$name'' was created.
Have fun!

See also chpass(1), finger(1), passwd(1)
EOF
    close M;
    return 1;
}

# read message file into buffer
sub message_read {
    local($file) = @_;
    @message_buffer = '';

    if (!open(R, "$file")) {
	warn "File ``$file'':$!\n"; return 0;
    }
    while(<R>) {
	push(@message_buffer, $_) unless /^\s*#/;
    }
    close R;
}

# write @list to $file with file-locking
sub append_file {
    local($file,@list) = @_;
    local($e);
    local($LOCK_EX) = 2;
    local($LOCK_NB) = 4;
    local($LOCK_UN) = 8;

    open(F, ">> $file") || die "$file: $!\n";
    print "Lock $file.\n" if $verbose > 1;
    while(!flock(F, $LOCK_EX | $LOCK_NB)) {
	warn "Cannot lock file: $file\a\n";
	die "Sorry, give up\n"
	    unless &confirm_yn("Try again?", "yes");
    }
    print F join("\n", @list) . "\n";
    close F;
    print "Unlock $file.\n" if $verbose > 1;
    flock(F, $LOCK_UN);
}

# return free uid+gid
# uid == gid if possible
sub next_id {
    local($group) = @_;

    $uid_start = 1000 if ($uid_start <= 0 || $uid_start >= $uid_end);
    # looking for next free uid
    while($uid{$uid_start}) {
	$uid_start++;
	$uid_start = 1000 if $uid_start >= $uid_end;
	print "$uid_start\n" if $verbose > 1;
    }

    local($gid_start) = $uid_start;
    # group for user (username==groupname) already exist
    if ($groupname{$group}) {
	$gid_start = $groupname{$group};
    }
    # gid is in use, looking for another gid.
    # Note: uid an gid are not equal
    elsif ($gid{$uid_start}) {
	while($gid{$gid_start} || $uid{$gid_start}) {
	    $gid_start--;
	    $gid_start = $uid_end if $gid_start < 100;
	}
    }
    return ($uid_start, $gid_start);
}

# read config file
sub config_read {
    local($opt) = @_;
    local($user_flag) = 0;

    # don't read config file
    return 1 if $opt =~ /-(noconfig|config_create)/ || !$config_read;

    if(!open(C, "$config")) {
	warn "$config: $!\n"; return 0;
    }

    while(<C>) {
	# user defined variables
	/^$do_not_delete/ && $user_flag++;
	# found @array or $variable
	if (s/^(\w+\s*=\s*\()/\@$1/ || s/^(\w+\s*=)/\$$1/) {
	    eval $_;
	    #warn "$_";
	}
	# lines with '^##' are not saved
	push(@user_variable_list, $_)
	    if $user_flag && !/^##/ && (s/^[\$\@]// || /^[#\s]/);
    }
    #warn "X @user_variable_list X\n";
    close C;
}


# write config file
sub config_write {
    local($silent) = @_;

    # nothing to do
    return 1 unless ($changes || ! -e $config || !$config_read || $silent);

    if (!$silent) {
	if (-e $config) {
	    return 1 if &confirm_yn("\nWrite your changes to $config?", "no");
	} else {
	    return 1 unless
		&confirm_yn("\nWrite your configuration to $config?", "yes");
	}
    }

    rename($config, "$config.bak");
    open(C, "> $config") || die "$config: $!\n";

    # prepare some variables
    $send_message = "no" unless $send_message;
    $defaultpasswd = "no" unless $defaultpasswd;
    local($shpref) = "'" . join("', '", @shellpref) . "'";
    local($shpath) = "'" . join("', '", @path) . "'";
    local($user_var) = join('', @user_variable_list);

    print C <<EOF;
#
# $config - automatic generated by adduser(8)
#
# Note: adduser read *and* write this file.
#	You may change values, but don't add new things before the
#	line ``$do_not_delete''
#

# verbose = [0-2]
verbose = $verbose

# regular expression usernames are checked against (see perlre(1))
# usernameregexp = 'regexp'
usernameregexp = '$usernameregexp'

# use password for new users
# defaultpasswd =  yes | no
defaultpasswd = $defaultpasswd

# copy dotfiles from this dir ("/usr/share/skel" or "no")
dotdir = "$dotdir"

# send this file to new user ("/etc/adduser.message" or "no")
send_message = "$send_message"

# config file for adduser ("/etc/adduser.conf")
config = "$config"

# logfile ("/var/log/adduser" or "no")
logfile = "$logfile"

# default HOME directory ("/home")
home = "$home"

# List of directories where shells located
# path = ('/bin', '/usr/bin', '/usr/local/bin')
path = ($shpath)

# common shell list, first element has higher priority
# shellpref = ('bash', 'tcsh', 'ksh', 'csh', 'sh')
shellpref = ($shpref)

# defaultshell if not empty ("bash")
defaultshell = "$defaultshell"

# defaultgroup ('USER' for same as username or any other valid group)
defaultgroup = $defaultgroup

# defaultclass if not empty
defaultclass = "$defaultclass"

# new users get this uid (1000)
uid_start = "$uid_start"

$do_not_delete
## your own variables, see /etc/adduser.message
$user_var

## end
EOF
    close C;
}

################
# main
#
$test = 0;	      # test mode, only for development
$check_only = 0;

&check_root;	    # you must be root to run this script!
&variables;	     # initialize variables
&config_read(@ARGV);	# read variables form config-file
&parse_arguments(@ARGV);    # parse arguments

if (!$check_only) {
    &copyright; &hints;
}

# check
$changes = 0;
&passwd_check;			# check for valid passwdb
&shells_read;			# read /etc/shells
&passwd_read;			# read /etc/master.passwd
&group_read;			# read /etc/group
&group_check;			# check for incon*
exit 0 if $check_only;		# only check consistence and exit


# interactive
# some questions
$usernameregexp = &usernameregexp_default; # regexp to check usernames against
&shells_add;			# maybe add some new shells
$defaultshell = &shell_default;	# enter default shell
$home = &home_partition($home);	# find HOME partition
$dotdir = &dotdir_default;	# check $dotdir
$send_message = &message_default;   # send message to new user
$defaultpasswd = &password_default; # maybe use password
&config_write(!$verbose);	# write variables in file

# main loop for creating new users
&new_users;	     # add new users

#end
