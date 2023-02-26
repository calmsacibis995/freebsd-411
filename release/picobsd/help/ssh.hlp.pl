[1mssh[m	Secure Shell

	Sposob uzycia: ssh [options] host [command]

	-l user     Log in using this user name.
	-n          Redirect input from /dev/null.
	-a          Disable authentication agent forwarding.
	-x          Disable X11 connection forwarding.
	-i file     Identity for RSA authentication (default: ~/.ssh/identity).
	-t          Tty; allocate a tty even if command is given.
	-v          Verbose; display verbose debugging messages.
	-V          Display version number only.
	-q          Quiet; don't display any warning messages.
	-f          Fork into background after authentication.
	-e char     Set escape character; ``none'' = disable (default: ~).
	-c cipher   Select encryption algorithm: ``idea'', ``3des''
	-p port     Connect to this port.  Server must be on the same port.
	-P          Don't use priviledged source port.
	-L listen-port:host:port   Forward local port to remote address
	-R listen-port:host:port   Forward remote port to local address
		These cause ssh to listen for connections on a port, and
		forward them to the other side by connecting to host:port.
	-C          Enable compression.
	-o 'option' Process the option as if it was read from a configuration
		file.

	Najczesciej uzywa sie w tej postaci:

		ssh -l nazwa_uzytk nazwa_maszyny
