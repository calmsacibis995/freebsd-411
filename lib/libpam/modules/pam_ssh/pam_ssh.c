/*-
 * Copyright (c) 1999, 2000 Andrew J. Korty
 * All rights reserved.
 * Copyright (c) 2001,2002 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * ThinkSec AS and NAI Labs, the Security Research Division of Network
 * Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: pam_ssh.c,v 1.23 2001/08/20 01:44:02 akorty Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD: src/lib/libpam/modules/pam_ssh/pam_ssh.c,v 1.28.2.4 2002/07/12 09:24:56 des Exp $");

#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PAM_SM_AUTH
#define PAM_SM_SESSION

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_mod_misc.h>

#include <openssl/dsa.h>
#include <openssl/evp.h>

#include "key.h"
#include "authfd.h"
#include "authfile.h"
#include "log.h"
#include "pam_ssh.h"

enum {
	PAM_OPT_KEYFILES = PAM_OPT_STD_MAX
};

static struct opttab other_options[] = {
	{ "keyfiles",	PAM_OPT_KEYFILES },
	{ NULL, 0 }
};

static void key_cleanup(pam_handle_t *, void *, int);
static void ssh_cleanup(pam_handle_t *, void *, int);

/*
 * Generic cleanup function for OpenSSH "Key" type.
 */

static void
key_cleanup(pam_handle_t *pamh __unused, void *data, int err __unused)
{
	if (data)
		key_free(data);
}


/*
 * Generic PAM cleanup function for this module.
 */

static void
ssh_cleanup(pam_handle_t *pamh __unused, void *data, int err __unused)
{
	if (data)
		free(data);
}


/*
 * Authenticate a user's key by trying to decrypt it with the password
 * provided.  The key and its comment are then stored for later
 * retrieval by the session phase.  An increasing index is embedded in
 * the PAM variable names so this function may be called multiple times
 * for multiple keys.
 */

static int
auth_via_key(pam_handle_t *pamh, const char *file, const char *dir,
    const struct passwd *user, const char *pass, struct options *options)
{
	char *comment;		/* private key comment */
	char *data_name;	/* PAM state */
	static int key_idx = 0;	/* for saved keys */
	Key *key;		/* user's key */
	char *path;		/* to key files */
	int retval;		/* from calls */

	/* locate the user's private key file */

	if (!asprintf(&path, "%s/%s", dir, file)) {
		PAM_LOG( "%m");
		return (PAM_SERVICE_ERR);
	}

	/* Try to decrypt the private key with the passphrase provided.  If
	   success, the user is authenticated. */

	comment = NULL;
	if ((retval = openpam_borrow_cred(pamh, user)) != PAM_SUCCESS)
		return (retval);
	key = key_load_private(path, pass, &comment);
	openpam_restore_cred(pamh);
	free(path);
	if (!comment)
		comment = strdup(file);
	if (!key) {
		free(comment);
		return (PAM_AUTH_ERR);
	}

	/* save the key and comment to pass to ssh-agent in the session
	   phase */

	if (!asprintf(&data_name, "ssh_private_key_%d", key_idx)) {
		PAM_LOG( "%m");
		free(comment);
		return (PAM_SERVICE_ERR);
	}
	retval = pam_set_data(pamh, data_name, key, key_cleanup);
	free(data_name);
	if (retval != PAM_SUCCESS) {
		key_free(key);
		free(comment);
		return (retval);
	}
	if (!asprintf(&data_name, "ssh_key_comment_%d", key_idx)) {
		PAM_LOG( "%m");
		free(comment);
		return (PAM_SERVICE_ERR);
	}
	retval = pam_set_data(pamh, data_name, comment, ssh_cleanup);
	free(data_name);
	if (retval != PAM_SUCCESS) {
		free(comment);
		return (retval);
	}

	++key_idx;
	return (PAM_SUCCESS);
}


/*
 * Add the keys stored by auth_via_key() to the agent connected to the
 * socket provided.
 */

static int
add_keys(pam_handle_t *pamh, struct options *options)
{
	AuthenticationConnection *ac;	/* connection to ssh-agent */
	char *comment;			/* private key comment */
	char *data_name;		/* PAM state */
	int final;			/* final return value */
	int key_idx;			/* for saved keys */
	Key *key;			/* user's private key */
	int retval;			/* from calls */

	/*
	 * Connect to the agent.
	 *
	 * XXX Because ssh_get_authentication_connection() gets the
	 * XXX agent parameters from the environment, we have to
	 * XXX temporarily replace the environment with the PAM
	 * XXX environment list.  This is a hack.
	 */
	{
		extern char **environ;
		char **saved, **evp;

		saved = environ;
		if ((environ = pam_getenvlist(pamh)) == NULL) {
			environ = saved;
			PAM_LOG( "%m");
			return (PAM_BUF_ERR);
		}
		ac = ssh_get_authentication_connection();
		for (evp = environ; *evp; evp++)
			free(*evp);
		free(environ);
		environ = saved;
	}
	if (!ac) {
		PAM_LOG( "%m");
		return (PAM_SESSION_ERR);
	}

	/* hand off each private key to the agent */

	final = 0;
	for (key_idx = 0; ; key_idx++) {
		if (!asprintf(&data_name, "ssh_private_key_%d", key_idx)) {
			PAM_LOG( "%m");
			ssh_close_authentication_connection(ac);
			return (PAM_SERVICE_ERR);
		}
		retval = pam_get_data(pamh, data_name, (const void **)&key);
		free(data_name);
		if (retval != PAM_SUCCESS)
			break;
		if (!asprintf(&data_name, "ssh_key_comment_%d", key_idx)) {
			PAM_LOG( "%m");
			ssh_close_authentication_connection(ac);
			return (PAM_SERVICE_ERR);
		}
		retval = pam_get_data(pamh, data_name,
		    (const void **)&comment);
		free(data_name);
		if (retval != PAM_SUCCESS)
			break;
		retval = ssh_add_identity(ac, key, comment);
		if (!final)
			final = retval;
	}
	ssh_close_authentication_connection(ac);

	return (final ? PAM_SUCCESS : PAM_SESSION_ERR);
}


PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	struct options options;
	int authenticated;		/* user authenticated? */
	char *dotdir;			/* .ssh dir name */
	char *file;			/* current key file */
	char *kfspec;			/* list of key files to add */
	char *keyfiles;
	const char *pass;		/* passphrase */
	const struct passwd *pwent;	/* user's passwd entry */
	struct passwd *pwent_keep;	/* our own copy */
	int retval;			/* from calls */
	const char *user;		/* username */

	pam_std_option(&options, other_options, argc, argv);

	if (!pam_test_option(&options, PAM_OPT_KEYFILES, &kfspec)) {
		kfspec = DEF_KEYFILES;
	}

	if ((retval = pam_get_user(pamh, &user, NULL)) != PAM_SUCCESS)
		return (retval);
	if (user == NULL || (pwent = getpwnam(user)) == NULL ||
	    pwent->pw_dir == NULL || pwent->pw_dir[0] == '\0')
		return (PAM_AUTH_ERR);

	/* pass prompt message to application and receive passphrase */

	retval = pam_get_pass(pamh, &pass, NEED_PASSPHRASE, &options);
	if (retval != PAM_SUCCESS)
		return (retval);

	OpenSSL_add_all_algorithms(); /* required for DSA */

	/* any key will authenticate us, but if we can decrypt all of the
	   specified keys, we'll do so here so we can cache them in the
	   session phase */

	if (!asprintf(&dotdir, "%s/%s", pwent->pw_dir, SSH_CLIENT_DIR)) {
		PAM_LOG( "%m");
		return (PAM_SERVICE_ERR);
	}
	authenticated = 0;
	keyfiles = strdup(kfspec);
	for (file = strtok(keyfiles, SEP_KEYFILES); file;
	     file = strtok(NULL, SEP_KEYFILES))
		if (auth_via_key(pamh, file, dotdir, pwent, pass, &options) ==
		    PAM_SUCCESS)
			authenticated++;
	free(keyfiles);
	free(dotdir);
	if (!authenticated)
		return (PAM_AUTH_ERR);

	/* copy the passwd entry (in case successive calls are made) and
	   save it for the session phase */

	if (!(pwent_keep = malloc(sizeof *pwent))) {
		PAM_LOG( "%m");
		return (PAM_SERVICE_ERR);
	}
	(void) memcpy(pwent_keep, pwent, sizeof *pwent_keep);
	if ((retval = pam_set_data(pamh, "ssh_passwd_entry", pwent_keep,
	    ssh_cleanup)) != PAM_SUCCESS) {
		free(pwent_keep);
		return (retval);
	}

	return (PAM_SUCCESS);
}


PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh __unused, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{

	return (PAM_SUCCESS);
}


PAM_EXTERN int
pam_sm_open_session(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	struct options options;
	char *agent_socket;		/* agent socket */
	char *env_end;			/* end of env */
	FILE *env_read;			/* env data source */
	char env_string[BUFSIZ];	/* environment string */
	char *env_value;		/* envariable value */
	int env_write;			/* env file descriptor */
	char hname[MAXHOSTNAMELEN];	/* local hostname */
	int no_link;			/* link per-agent file? */
	char *per_agent;		/* to store env */
	char *per_session;		/* per-session filename */
	char *agent_pid;		/* agent pid */
	const struct passwd *pwent;	/* user's passwd entry */
	int retval;			/* from calls */
	int start_agent;		/* start agent? */
	const char *tty;		/* tty or display name */

	pam_std_option(&options, other_options, argc, argv);

	/* dump output of ssh-agent in ~/.ssh */
	if ((retval = pam_get_data(pamh, "ssh_passwd_entry",
	    (const void **)&pwent)) != PAM_SUCCESS)
		return (retval);

	/*
	 * Use reference counts to limit agents to one per user per host.
	 *
	 * Technique: Create an environment file containing
	 * information about the agent.  Only one file is created, but
	 * it may be given many names.  One name is given for the
	 * agent itself, agent-<host>.  Another name is given for each
	 * session, agent-<host>-<display> or agent-<host>-<tty>.  We
	 * delete the per-session filename on session close, and when
	 * the link count goes to unity on the per-agent file, we
	 * delete the file and kill the agent.
	 */

	/* the per-agent file contains just the hostname */

	(void) gethostname(hname, sizeof hname);
	if (asprintf(&per_agent, "%s/.ssh/agent-%s", pwent->pw_dir, hname)
	    == -1) {
		PAM_LOG( "%m");
		return (PAM_SERVICE_ERR);
	}

	/* save the per-agent filename in case we want to delete it on
	   session close */

	if ((retval = pam_set_data(pamh, "ssh_agent_env_agent", per_agent,
	    ssh_cleanup)) != PAM_SUCCESS) {
		free(per_agent);
		return (retval);
	}

	/* take on the user's privileges for writing files and starting the
	   agent */

	if ((retval = openpam_borrow_cred(pamh, pwent)) != PAM_SUCCESS)
		return (retval);

	/* Try to create the per-agent file or open it for reading if it
	   exists.  If we can't do either, we won't try to link a
	   per-session filename later.  Start the agent if we can't open
	   the file for reading. */

	env_write = no_link = 0;
	env_read = NULL;
	if ((env_write = open(per_agent, O_CREAT | O_EXCL | O_WRONLY,
	    S_IRUSR)) < 0 && !(env_read = fopen(per_agent, "r")))
		no_link = 1;
	if (env_read) {
		start_agent = 0;
		openpam_restore_cred(pamh);
	} else {
		start_agent = 1;
		env_read = popen(SSH_AGENT, "r");
		openpam_restore_cred(pamh);
		if (!env_read) {
			PAM_LOG( "%s: %m", SSH_AGENT);
			if (env_write >= 0)
				(void) close(env_write);
			return (PAM_SESSION_ERR);
		}
	}

	/* save environment for application with pam_putenv() */

	agent_socket = NULL;
	while (fgets(env_string, sizeof env_string, env_read)) {

		/* parse environment definitions */

		if (env_write >= 0)
			(void) write(env_write, env_string,
			    strlen(env_string));
		if (!(env_value = strchr(env_string, '=')) ||
		    !(env_end = strchr(env_value, ';')))
			continue;
		*env_end = '\0';

		/* pass to the application */

		if (!((retval = pam_putenv(pamh, env_string)) ==
		    PAM_SUCCESS)) {
			if (start_agent)
				(void) pclose(env_read);
			else
				(void) fclose(env_read);
			if (env_write >= 0)
				(void) close(env_write);
			if (agent_socket)
				free(agent_socket);
			return (PAM_SERVICE_ERR);
		}

		*env_value++ = '\0';

		/* save the agent socket so we can connect to it and add
		   the keys as well as the PID so we can kill the agent on
		   session close. */

		if (strcmp(&env_string[strlen(env_string) -
		    strlen(ENV_SOCKET_SUFFIX)], ENV_SOCKET_SUFFIX) == 0 &&
		    !(agent_socket = strdup(env_value))) {
			PAM_LOG( "%m");
			if (start_agent)
				(void) pclose(env_read);
			else
				(void) fclose(env_read);
			if (env_write >= 0)
				(void) close(env_write);
			if (agent_socket)
				free(agent_socket);
			return (PAM_SERVICE_ERR);
		} else if (strcmp(&env_string[strlen(env_string) -
		    strlen(ENV_PID_SUFFIX)], ENV_PID_SUFFIX) == 0 &&
		    ((agent_pid = strdup(env_value)) == NULL ||
		    (retval = pam_set_data(pamh, "ssh_agent_pid",
		    agent_pid, ssh_cleanup)) != PAM_SUCCESS)) {
			if (start_agent)
				(void) pclose(env_read);
			else
				(void) fclose(env_read);
			if (env_write >= 0)
				(void) close(env_write);
			if (agent_socket)
				free(agent_socket);
			if (agent_pid)
				free(agent_pid);
			return (retval);
		}

	}
	if (env_write >= 0)
		(void) close(env_write);

	if (start_agent) {
		switch (retval = pclose(env_read)) {
		case -1:
			PAM_LOG( "%s: %m", SSH_AGENT);
			if (agent_socket)
				free(agent_socket);
			return (PAM_SESSION_ERR);
		case 0:
			break;
		case 127:
			PAM_LOG( "cannot execute %s",
			    SSH_AGENT);
			if (agent_socket)
				free(agent_socket);
			return (PAM_SESSION_ERR);
		default:
			PAM_LOG( "%s exited %s %d",
			    SSH_AGENT, WIFSIGNALED(retval) ? "on signal" :
			    "with status", WIFSIGNALED(retval) ?
			    WTERMSIG(retval) : WEXITSTATUS(retval));
			if (agent_socket)
				free(agent_socket);
			return (PAM_SESSION_ERR);
		}
	} else
		(void) fclose(env_read);

	if (!agent_socket)
		return (PAM_SESSION_ERR);

	if (start_agent && (retval = add_keys(pamh, &options))
	    != PAM_SUCCESS)
		return (retval);
	free(agent_socket);

	/* if we couldn't access the per-agent file, don't link a
	   per-session filename to it */

	if (no_link)
		return (PAM_SUCCESS);

	/* the per-session file contains the display name or tty name as
	   well as the hostname */

	if ((retval = pam_get_item(pamh, PAM_TTY, (const void **)&tty))
	    != PAM_SUCCESS)
		return (retval);
	if (asprintf(&per_session, "%s/.ssh/agent-%s-%s", pwent->pw_dir,
	    hname, tty) == -1) {
		PAM_LOG( "%m");
		return (PAM_SERVICE_ERR);
	}

	/* save the per-session filename so we can delete it on session
	   close */

	if ((retval = pam_set_data(pamh, "ssh_agent_env_session",
	    per_session, ssh_cleanup)) != PAM_SUCCESS) {
		free(per_session);
		return (retval);
	}

	(void) unlink(per_session);		/* remove cruft */
	(void) link(per_agent, per_session);

	return (PAM_SUCCESS);
}


PAM_EXTERN int
pam_sm_close_session(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	struct options options;
	const char *env_file;		/* ssh-agent environment */
	pid_t pid;			/* ssh-agent process id */
	int retval;			/* from calls */
	const char *ssh_agent_pid;	/* ssh-agent pid string */
	struct stat sb;			/* to check st_nlink */

	pam_std_option(&options, other_options, argc, argv);

	if ((retval = pam_get_data(pamh, "ssh_agent_env_session",
	    (const void **)&env_file)) == PAM_SUCCESS && env_file)
		(void) unlink(env_file);

	/* Retrieve per-agent filename and check link count.  If it's
	   greater than unity, other sessions are still using this
	   agent. */

	if ((retval = pam_get_data(pamh, "ssh_agent_env_agent",
	    (const void **)&env_file)) == PAM_SUCCESS && env_file &&
	    stat(env_file, &sb) == 0) {
		if (sb.st_nlink > 1)
			return (PAM_SUCCESS);
		(void) unlink(env_file);
	}

	/* retrieve the agent's process id */

	if ((retval = pam_get_data(pamh, "ssh_agent_pid",
	    (const void **)&ssh_agent_pid)) != PAM_SUCCESS)
		return (retval);

	/* Kill the agent.  SSH's ssh-agent does not have a -k option, so
	   just call kill(). */

	pid = atoi(ssh_agent_pid);
	if (pid <= 0)
		return (PAM_SESSION_ERR);
	if (kill(pid, SIGTERM) != 0) {
		PAM_LOG( "%s: %m", ssh_agent_pid);
		return (PAM_SESSION_ERR);
	}

	return (PAM_SUCCESS);
}

PAM_MODULE_ENTRY(MODULE_NAME);
