/*
 * daytimed-sctp/daytimed.c --
 *
 * A simple SCTP over IPv4/IPv6 daytime server. The server waits for
 * incoming connections, sends a daytime string as a reaction to
 * successful connection establishment and finally closes the
 * connection down again.
 *
 * This code idea is borrowed from M. Tim Jones (IBM):
 * <http://www.ibm.com/developerworks/linux/library/l-sctp/>
 */

#define _POSIX_C_SOURCE 2

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/sctp.h>

static const char *progname = "daytimed";

#ifndef NI_MAXHOST
#define NI_MAXHOST      1025
#endif
#ifndef NI_MAXSERV
#define NI_MAXSERV      32
#endif

/*
 * Store the current date and time of the day using the local timezone
 * in the given buffer of the indicated size. Set the buffer to a zero
 * length string in case of errors.
 */

static void
daytime(char *buffer, size_t size, int local)
{
    time_t ticks;
    struct tm *tm;

    ticks = time(NULL);
    if (local) {
	tm = localtime(&ticks);
    } else {
	tm = gmtime(&ticks);
    }
    if (tm == NULL) {
        buffer[0] = '\0';
        syslog(LOG_ERR, "localtime or gmtime failed");
        return;
    }
    strftime(buffer, size, "%F %T\r\n", tm);
}

/*
 * Create a listening SCTP endpoint. First get the list of potential
 * network layter addresses and transport layer port numbers. Iterate
 * through the returned address list until an attempt to create a
 * listening SCTP endpoint is successful (or no other alternative
 * exists).
 */

static int
sctp_listen(char *port)
{
    struct addrinfo hints, *ai_list, *ai;
    int n, fd = 0, on = 1;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    n = getaddrinfo(NULL, port, &hints, &ai_list);
    if (n) {
        fprintf(stderr, "%s: getaddrinfo failed: %s\n",
                progname, gai_strerror(n));
        return -1;
    }

    for (ai = ai_list; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, IPPROTO_SCTP);
        if (fd < 0) {
            continue;
        }

        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(fd);
    }

    freeaddrinfo(ai_list);

    if (ai == NULL) {
        fprintf(stderr, "%s: bind failed for port %s\n",
                progname, port);
        return -1;
    }

    if (listen(fd, 42) < 0) {
        fprintf(stderr, "%s: listen failed: %s\n",
                progname, strerror(errno));
	close(fd);
        return -1;
    }

    return fd;
}

/*
 * Accept a new SCTP connection and write a message about who was
 * accepted to the system log.
 */

static int
sctp_accept(int listen)
{
    struct sockaddr_storage ss;
    socklen_t ss_len = sizeof(ss);
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    int n, fd;

    fd = accept(listen, (struct sockaddr *) &ss, &ss_len);
    if (fd == -1) {
        syslog(LOG_ERR, "accept failed: %s", strerror(errno));
        return -1;
    }

    n = getnameinfo((struct sockaddr *) &ss, ss_len,
                    host, sizeof(host), serv, sizeof(serv),
                    NI_NUMERICHOST);
    if (n) {
        syslog(LOG_ERR, "getnameinfo failed: %s", gai_strerror(n));
    } else {
        syslog(LOG_DEBUG, "connection from %s:%s", host, serv);
    }

    return fd;
}

/*
 * Close a SCTP connection. This function trivially calls close() on
 * POSIX systems, but might be more complicated on other systems.
 */

static int
sctp_close(int fd)
{
    return close(fd);
}

/*
 * Implement the daytime protocol, loosely modeled after RFC 867.
 */

static void
sctp_daytime(int listenfd)
{
    size_t n;
    int client;
    char message[128];

    client = sctp_accept(listenfd);
    if (client == -1) {
        return;
    }

#define DAYTIME_STREAM_LOCAL 0
#define DAYTIME_STREAM_GMT 1

    daytime(message, sizeof(message), 1);
    n = sctp_sendmsg(client, message, strlen(message),
		     NULL, 0, 0, 0, DAYTIME_STREAM_LOCAL, 0, 0 );
    if (n != strlen(message)) {
        syslog(LOG_ERR, "sctp_sendmsg failed (daytime_stream_local)");
    }
    
    daytime(message, sizeof(message), 0);
    n = sctp_sendmsg(client, message, strlen(message),
		     NULL, 0, 0, 0, DAYTIME_STREAM_GMT, 0, 0 );
    if (n != strlen(message)) {
        syslog(LOG_ERR, "sctp_sendmsg failed (daytime_stream_gmt)");
    }

    sctp_close(client);
}


int
main(int argc, char **argv)
{
    int tfd;
    
    if (argc != 2) {
        fprintf(stderr, "usage: %s port\n", progname);
        exit(EXIT_FAILURE);
    }

    openlog(progname, LOG_PID, LOG_DAEMON);

    tfd = sctp_listen(argv[1]);

    while (tfd != -1) {
        sctp_daytime(tfd);
    }

    sctp_close(tfd);

    closelog();

    return EXIT_SUCCESS;
}
