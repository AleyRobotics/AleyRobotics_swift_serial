/*
 * Copyright 2011 Kevin Cernekee <cernekee@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <termios.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <arpa/telnet.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BUFLEN			256

static int esc_char = 0x1e;		/* ^^ (control-shift-6) */
static char *devpath = NULL;
static int max_fd = 0;
static fd_set client_fds;
static int num_clients = 0;
static int device_fd = -1;
static char *reboot_cmd = NULL;
static int baud = 115200;

char boardname[16] = "";

#define __weak __attribute__((weak))

static void die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	exit(1);
}

void usage(void)
{
	printf("usage: ip2ser [ options ] -d <device>\n");
	printf("\n");
	printf("Options:\n");
	printf(" -d <device>          Serial device (e.g. /dev/ttyS0)\n");
	printf(" -p <port>            TCP port (default 2300)\n");
	printf(" -b <baud>            Baud rate (default 115200)\n");
	printf(" -e <esc_char>        Escape character (default 0x1e = Control-^)\n");
	printf(" -R                   Raw protocol (default is telnet)\n");
	printf(" -r <reboot_cmd>      Shell command line to reboot the target\n");
	printf(" -D                   Debug mode - don't fork into background\n");
	exit(1);
}

void __weak set_boardname(unsigned char *buf, int len)
{
	/*
	 * This function can be modified to scan each line from the target
	 * to determine the board type.  e.g. from the banner printed by a
	 * bootloader.  This can be helpful if you have a large number of
	 * systems/ports to keep straight.
	 *
	 * boardname will be printed when connecting to ip2ser:
	 *
	 * Trying 127.0.0.1...
	 * Connected to localhost.
	 * Escape character is '^]'.
	 *
	 * *** Connected to /dev/ttyRP0 (myboard) at 115200 bps
	 * *** Host: 127.0.0.1:2300
	 * *** Client: 127.0.0.1:50589
	 * *** Other clients: 2
	 * *** For help: <Control-^> ?
	 */
}

static void write_all(unsigned char *buf, int len)
{
	int i;
	for (i = 0; i <= max_fd; i++)
		if (FD_ISSET(i, &client_fds))
			write(i, buf, len);
	set_boardname(buf, len);
}

static void print_one(int fd, const char *fmt, ...)
{
	char msg[BUFLEN];
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(msg, BUFLEN, fmt, ap);
	va_end(ap);

	write(fd, msg, len);
}

static void print_all(const char *fmt, ...)
{
	char msg[BUFLEN];
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(msg, BUFLEN, fmt, ap);
	va_end(ap);

	write_all((unsigned char *)msg, len);
}

static void write_status(int fd)
{
	struct sockaddr_in remote_sock, local_sock;
	socklen_t socklen;
	char msg[BUFLEN], *ptr = msg;
	char esc_name[BUFLEN];

	ptr += sprintf(ptr,
		"\r\n*** Connected to %s%s at %d bps\r\n",
			devpath, boardname, baud);

	socklen = sizeof(local_sock);
	if (getsockname(fd, (struct sockaddr *)&local_sock, &socklen) >= 0)
		ptr += sprintf(ptr, "*** Host: %s:%d\r\n",
			inet_ntoa(local_sock.sin_addr),
			ntohs(local_sock.sin_port));

	socklen = sizeof(remote_sock);
	if (getpeername(fd, (struct sockaddr *)&remote_sock, &socklen) >= 0)
		ptr += sprintf(ptr, "*** Client: %s:%d\r\n",
			inet_ntoa(remote_sock.sin_addr),
			ntohs(remote_sock.sin_port));

	ptr += sprintf(ptr, "*** Other clients: %d\r\n",
		num_clients - 1);

	switch (esc_char) {
	case 0x1c:
		strcpy(esc_name, "Control-\\");
		break;
	case 0x1d:
		strcpy(esc_name, "Control-]");
		break;
	case 0x1e:
		strcpy(esc_name, "Control-^");
		break;
	case 0x1f:
		strcpy(esc_name, "Control-_");
		break;
	default:
		if ((esc_char >= 0x01) && (esc_char <= 0x1a))
			sprintf(esc_name, "Control-%c", esc_char + 0x40);
		else
			sprintf(esc_name, "UNKNOWN");
	}

	ptr += sprintf(ptr, "*** For help: <%s> ?\r\n", esc_name);

	write(fd, msg, strlen(msg));
}

static void set_baud(int newbaud, int broadcast)
{
	baud = newbaud;
	struct termios termios;

	if (tcgetattr(device_fd, &termios) != 0)
		die("can't tcgetattr: %s\n", strerror(errno));

	termios.c_iflag = 0;
	termios.c_oflag = 0;
	termios.c_cflag = CS8 | CLOCAL | CREAD;
	termios.c_lflag = 0;
	switch (baud) {
        case 460800: cfsetspeed(&termios, B460800); break;
        case 230400: cfsetspeed(&termios, B230400); break;
		case 115200: cfsetspeed(&termios, B115200); break;
		case 57600: cfsetspeed(&termios, B57600); break;
		case 38400: cfsetspeed(&termios, B38400); break;
		case 19200: cfsetspeed(&termios, B19200); break;
		case 9600: cfsetspeed(&termios, B9600); break;
		default:
			die("unsupported baud rate: %d\n", baud);
	}

	if (tcsetattr(device_fd, TCSANOW, &termios) != 0)
		die("can't tcsetattr: %s\n", strerror(errno));

	if (broadcast)
		print_all("*** Baud rate set to %d bps\r\n", baud);
}

static int get_lockname(char *dev, char *buf)
{
	char *tmp;
	if (strlen(dev) > (BUFLEN / 2))
		return -1;

	tmp = strrchr(dev, '/');
	if (tmp)
		dev = tmp + 1;

	sprintf(buf, "/var/lock/LCK..%s", dev);
	return 0;
}

static int write_lockfile(char *lockname)
{
	int fd, len;
	char buf[BUFLEN];

	fd = open(lockname, O_RDWR | O_CREAT | O_EXCL, 0644);
	if (fd < 0)
		return -1;
	len = sprintf(buf, "%10d ip2ser root\n", getpid());
	write(fd, buf, len);
	close(fd);
	return 0;
}

static int lock_tty(char *name)
{
	char lockname[BUFLEN], buf[BUFLEN];
	int fd, len, pid = 0;

	/* /var/lock might not even be accessible */
	if (access("/var/lock", R_OK | W_OK) < 0)
		return 0;

	if (get_lockname(name, lockname) == -1)
		return -1;

	fd = open(lockname, O_RDONLY);
	if (fd < 0)
		return write_lockfile(lockname);

	len = read(fd, buf, BUFLEN - 1);
	close(fd);

	if (len < 0)
		return -1;

	buf[len] = 0;

	sscanf(buf, "%d", &pid);
	if (pid != 0) {
		/* somebody else really locked the device */
		if (kill(pid, 0) >= 0)
			return -1;
	}
	/* stale lock - take it over */
	unlink(lockname);
	return write_lockfile(lockname);
}

static void unlock_tty(char *name)
{
	char lockname[BUFLEN];

	if (get_lockname(name, lockname) != -1)
		unlink(lockname);
}

static int open_tty(char *name)
{
	if (lock_tty(name) < 0) {
		print_all("\r\n*** Device is locked, disconnecting\r\n\r\n");
		return -1;
	}
	device_fd = open(name, O_RDWR | O_NOCTTY);
	if (device_fd < 0) {
		print_all("*** Can't open device: %s\r\n", strerror(errno));
		return -1;
	}
	if (device_fd > max_fd)
		max_fd = device_fd;
	set_baud(baud, 0);
	printf("OPENED: %s\n", name);
	return 0;
}

static void close_tty(char *name)
{
	if (device_fd == -1)
		return;
	close(device_fd);
	device_fd = -1;
	unlock_tty(name);
	printf("CLOSED: %s\n", name);
}

static void disconnect(int fd)
{
	printf("DISCONNECT: fd %d\n", fd);
	close(fd);
	FD_CLR(fd, &client_fds);
	num_clients--;

	if (num_clients == 0)
		close_tty(devpath);
}

static int cleanup_input(int fd, unsigned char *buf, int len)
{
	unsigned char *start = buf, *out = buf;
	static int cmd_active = 0;
	int i;

	while (len > 0) {
		/* process user commands */
		if (cmd_active) {
			cmd_active = 0;
			switch (*buf) {
			case 'b':
			case 'B':	
				/* send serial BREAK */
				tcsendbreak(device_fd, 0);
				break;
			case 'c':
			case 'C':	
				/* clear the screen */
				print_all("\e[2J\e[1;1H");
				break;
			case 'e':
			case 'E':
				/* exclusive access */
				for (i = 0; i <= max_fd; i++)
					if (i != fd &&
					    FD_ISSET(i, &client_fds)) {
						disconnect(i);
					}
				break;
			case 'r':
			case 'R':
				/* reboot target */
				if (reboot_cmd == NULL)
					print_all("Reboot command is unset\r\n");
				else {
					print_all("\r\n*** REBOOTING TARGET\r\n");
					system(reboot_cmd);
				}
				break;
			case 's':
			case 'S':
				/* status check */
				write_status(fd);
				break;
			case 't':
			case 'T':
				/* tty reset */
				print_all("\ec\e!p");
				break;
			case '.':
				/* terminate connection */
				disconnect(fd);
				return 0;
			case '1':
				set_baud(115200, 1);
				break;
			case '5':
				set_baud(57600, 1);
				break;
			case '3':
				set_baud(38400, 1);
				break;
			case '2':
				set_baud(19200, 1);
				break;
			case '9':
				set_baud(9600, 1);
				break;
			case '?':
				print_one(fd, "\r\n");
				print_one(fd, "Supported escape sequences:\r\n");
				print_one(fd, ". - terminate connection\r\n");
				print_one(fd, "B - send a BREAK to the device\r\n");
				print_one(fd, "C - clear the screen\r\n");
				print_one(fd, "E - exclusive access "
					"(kill other clients)\r\n");
				print_one(fd, "R - reboot the target\r\n");
				print_one(fd, "S - status\r\n");
				print_one(fd, "T - tty reset\r\n");
				print_one(fd, "1,5,3,2,9 - set port to "
					"(115200,57600,38400,19200,9600) "
					"bps\r\n");
				print_one(fd, "? - this help page\r\n");
				break;
			default:
				if (*buf == esc_char) {
					*out++ = esc_char;
				} else {
					continue;	/* handle as literal */
				}
			}
			printf("CMD: fd %d arg '%c'\n", fd, *buf);
			buf++;
			len--;
			continue;
		}
		/* ignore all telnet options */
		if (*buf == IAC) {
			/* DO/DONT/WILL/WONT 3-byte sequences */
			if ((buf[1] >= WILL) && (buf[1] <= DONT) && (len > 2)) {
				buf += 3;
				len -= 3;
				continue;
			}
			/* other 2-byte sequences */
			if (len >= 2) {
				buf += 2;
				len -= 2;
				continue;
			}
		}
		/* fix up erase character */
		if (*buf == 0x7f) {
			*out++ = 0x08;
			buf++;
			len--;
			continue;
		}
		/* fix up cr/lf */
		if ((len >= 2) && buf[0] == 0x0d &&
		    (buf[1] == 0x0a || buf[1] == 0x00)) {
			buf += 2;
			*out++ = '\r';
			len -= 2;
			continue;
		}
		/* special user commands */
		if (*buf == esc_char) {
			cmd_active = 1;
			buf++;
			len--;
			continue;
		}

		*out++ = *buf++;
		len--;
	}
	return out - start;
}

static void cleanup_and_exit(int sig, siginfo_t *siginfo, void *data)
{
	close_tty(devpath);
	exit(1);
}

static void setup_signals(void)
{
	struct sigaction s;

	/* don't crash if a telnet connection drops */
	memset(&s, 0, sizeof(s));
	s.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &s, NULL);

	/* clean up the lockfile (if present) before terminating */
	memset(&s, 0, sizeof(s));
	s.sa_sigaction = cleanup_and_exit;
	sigaction(SIGTERM, &s, NULL);
	sigaction(SIGINT, &s, NULL);
	sigaction(SIGHUP, &s, NULL);
}

int main(int argc, char **argv)
{
	int port = 2300;
	int yes = 1, listen_fd, opt;
	struct sockaddr_in addr;
	fd_set all_fds;
	int foreground = 0, raw = 0;

	while ((opt = getopt(argc, argv, "d:p:b:e:r:DR")) != -1) {
		switch (opt) {
		case 'd':
			devpath = optarg;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'b':
			baud = atoi(optarg);
			break;
		case 'e':
			esc_char = strtol(optarg, NULL, 0);
			break;
		case 'r':
			reboot_cmd = optarg;
			break;
		case 'D':
			foreground = 1;
			break;
		case 'R':
			raw = 1;
			break;
		default:
			usage();
		}
	}

	if (!devpath)
		usage();

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0)
		die("can't create socket: %s\n", strerror(errno));

	if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
		       &yes, sizeof(yes)) < 0)
		die("can't set socket options: %s\n", strerror(errno));

	memset(&addr, 0, sizeof(addr));
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_family = AF_INET;

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
		die("can't bind: %s\n", strerror(errno));
	if (listen(listen_fd, 8) < 0)
		die("can't listen: %s\n", strerror(errno));
	if (fcntl(listen_fd, F_SETFL, O_NONBLOCK) < 0)
		die("can't fcntl: %s\n", strerror(errno));

	if (access(devpath, R_OK | W_OK) < 0)
		die("can't open tty: %s\n", strerror(errno));

	FD_ZERO(&client_fds);
	num_clients = 0;
	max_fd = listen_fd;

	if (!foreground) {
		pid_t p = fork();
		int fd;

		if (p < 0)
			die("fork failed: %s\n", strerror(errno));
		if (p > 0)
			_exit(0);
		setsid();
		close(fileno(stdin));
		close(fileno(stdout));
		close(fileno(stderr));
		fd = open("/dev/null", O_RDONLY);
		dup(fd);
		dup(fd);
	}

	setup_signals();

	while (1) {
		int fds;

		all_fds = client_fds;
		if (device_fd != -1)
			FD_SET(device_fd, &all_fds);
		FD_SET(listen_fd, &all_fds);
		fds = select(max_fd + 1, &all_fds, NULL, NULL, NULL);

		/* check for new connections */

		if (FD_ISSET(listen_fd, &all_fds)) {
			struct sockaddr_in sock;
			socklen_t socklen = sizeof(sock);
			int newfd = accept(listen_fd,
				(struct sockaddr *)&sock, &socklen);
			if (newfd > 0) {
				const char opts[] = {
					IAC, DO, TELOPT_ECHO,
					IAC, DO, TELOPT_LFLOW,
					IAC, WILL, TELOPT_ECHO,
					IAC, WILL, TELOPT_SGA,
				};

				printf("CONNECT: fd %d ip %s\n", newfd,
					inet_ntoa(sock.sin_addr));

				fcntl(newfd, F_SETFL, O_NONBLOCK);
				if (!raw)
					write(newfd, opts, sizeof(opts));
				FD_SET(newfd, &client_fds);
				if (newfd > max_fd)
					max_fd = newfd;
				num_clients++;

				if (num_clients == 1) {
					if (open_tty(devpath) < 0) {
						/* can't open tty */
						disconnect(newfd);
						newfd = -1;
					}
				}
				if (!raw && newfd != -1) {
					write_status(newfd);
					write(newfd, "\r\n", 2);
				}
			}
			fds--;
		}

		/* check for new data on the serial port */

		if (device_fd != -1 && FD_ISSET(device_fd, &all_fds)) {
			int len;
			unsigned char buf[BUFLEN];

			len = read(device_fd, buf, BUFLEN);

			if (len > 0) {
				int i;
				/*
				 * remove anything resembling the telnet
				 * escape sequence
				 */
				if (!raw)
					for (i = 0; i < len; i++)
						if(buf[i] == 0xff)
							buf[i] = 0x7f;
				write_all(buf, len);
			}
			fds--;
		}

		/* anything else is activity on existing telnet connections */

		if (fds > 0) {
			int i;

			for (i = 0; i <= max_fd; i++) {
				if (FD_ISSET(i, &all_fds) &&
				    FD_ISSET(i, &client_fds)) {
					int len;
					unsigned char buf[BUFLEN];

					len = read(i, buf, BUFLEN);

					/* hang up? */
					if (len <= 0)
						disconnect(i);
					else {
						if (!raw)
							len = cleanup_input(i,
								buf, len);
						if (len)
							write(device_fd, buf,
								len);
					}
				}
			}
		}
	} /* while (1) */
}
