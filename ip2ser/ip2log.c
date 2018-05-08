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

#define _FILE_OFFSET_BITS	64
#define _GNU_SOURCE

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
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/telnet.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>

#define BUFLEN			256
#define MAX_LINE		4096

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
	printf("usage: ip2log [ options ] <host> <port>\n");
	printf("\n");
	printf("Options:\n");
	printf(" -f <file>            Log to FILE (default: HOST-PORT.txt)\n");
	printf(" -a                   Append to log file (default: overwrite)\n");
	printf(" -R                   Raw mode - no character translation\n");
	printf(" -t                   Enable standard timestamps\n");
	printf(" -tt                  Enable microsecond timestamps\n");
	printf(" -D                   Debug mode - don't fork into background\n");
	exit(1);
}

int open_sock(char *host, int port)
{
	int err, fd;
	struct addrinfo *a, hints;
	char ports[32];

	sprintf(ports, "%u", port);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_NUMERICSERV;

	err = getaddrinfo(host, ports, &hints, &a);
	if (err != 0)
		die("getaddrinfo failed: %s\n", gai_strerror(err));

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		die("socket failed: %s\n", strerror(errno));

	if (connect(fd, a[0].ai_addr, a[0].ai_addrlen) < 0)
		die("connect failed: %s\n", strerror(errno));

	freeaddrinfo(a);

	return fd;
}

static char tcp_buf[MAX_LINE];
static int tcp_buf_pos = 0;
static int tcp_buf_len = 0;

int get_byte(int fd)
{
	int ret;
	fd_set fds;

	if (tcp_buf_pos < tcp_buf_len) {
		tcp_buf_pos++;
		return (unsigned char)tcp_buf[tcp_buf_pos - 1];
	}

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	if (select(fd + 1, &fds, NULL, NULL, NULL) < 0)
		return -1;

	ret = read(fd, tcp_buf, MAX_LINE);
	if (ret <= 0)
		return -1;

	tcp_buf_pos = 1;
	tcp_buf_len = ret;

	return (unsigned char)tcp_buf[0];
}

static int timestamp = 0;
static char line_buf[MAX_LINE];
static int line_buf_pos = 0;

static void line_buf_flush(int fd)
{
	char buf[BUFLEN] = "";
	int len = 0;
	struct timeval tv;
	struct tm *tm;

	gettimeofday(&tv, NULL);
	tm = localtime(&tv.tv_sec);

	if (timestamp == 1)
		len = snprintf(buf, BUFLEN, "[%02d/%02d %02d:%02d:%02d] ",
			tm->tm_mon + 1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	else if(timestamp == 2)
		len = snprintf(buf, BUFLEN, "[%02d/%02d %02d:%02d:%02d.%06d] ",
			tm->tm_mon + 1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec,
			(int)tv.tv_usec);

	lseek(fd, 0, SEEK_END);
	if (len)
		write(fd, buf, len);

	if (line_buf_pos < (MAX_LINE - 1)) {
		sprintf(&line_buf[line_buf_pos], "\n");
		line_buf_pos += 1;
	}

	write(fd, line_buf, line_buf_pos);
	line_buf_pos = 0;
}

static void line_buf_putc(int fd, char c)
{
	if (line_buf_pos == MAX_LINE) {
		line_buf_flush(fd);
		line_buf_pos = sprintf(line_buf, "<TRUNCATED LINE>");
		line_buf_flush(fd);
		return;
	}

	line_buf[line_buf_pos] = c;
	line_buf_pos++;
}

int main(int argc, char **argv)
{
	int port = 0, ret;
	char *host, *file = NULL, *tmp;
	int foreground = 0, raw = 0;
	int log_fd, sock_fd, opt, append = 0;

	while ((opt = getopt(argc, argv, "tRDaf:")) != -1) {
		switch (opt) {
		case 'f':
			file = optarg;
			break;
		case 't':
			timestamp++;
			break;
		case 'a':
			append = 1;
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

	if (optind >= argc)
		usage();

	host = strdup(argv[optind]);

	tmp = strchr(host, ':');
	if (tmp) {
		*tmp = 0;
		port = strtoul(tmp + 1, NULL, 0);
	}

	if ((optind + 1) < argc)
		port = strtoul(argv[optind + 1], NULL, 0);

	if (!port)
		usage();

	if (!file) {
		file = malloc(strlen(host) + 16);
		sprintf(file, "%s-%d.txt", host, port);
	}

	sock_fd = open_sock(host, port);

	log_fd = open(file, O_WRONLY | O_CREAT | (append ? 0 : O_TRUNC), 0644);
	if (log_fd < 0)
		die("can't open %s: %s\n", file, strerror(errno));

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

	line_buf_pos = sprintf(line_buf, "%%%%%% Connected to %s:%d",
		host, port);
	line_buf_flush(log_fd);

	while (1) {
		unsigned char b;

		ret = get_byte(sock_fd);
		if (ret < 0)
			break;
		b = (char)ret;
		if (raw) {
			write(log_fd, (void *)&b, 1);
			continue;
		}

		switch (ret) {
		case 0xff:
			/* telnet command - ignore */
			get_byte(sock_fd);
			get_byte(sock_fd);
			break;
		case 0x0d:
			/* CR/LF */
			line_buf_flush(log_fd);
			ret = get_byte(sock_fd);
			if (ret != 0x0a)
				line_buf_putc(log_fd, ret);
			break;
		case 0x0a:
			/* LF */
			line_buf_flush(log_fd);
			break;
		case 0x07:
			/* Bell */
			break;
		case 0x08:
			/* Backspace */
			if (line_buf_pos)
				line_buf_pos--;
			break;
		default:
			line_buf_putc(log_fd, ret);
		}
	}

	line_buf_flush(log_fd);
	line_buf_pos = sprintf(line_buf, "%%%%%% Connection closed");
	line_buf_flush(log_fd);

	close(log_fd);
	close(sock_fd);

	return 0;
}
