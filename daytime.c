/*
 * daytime-scpt/daytime.c --
 *
 * A simple SCTP over IPv4/IPv6 daytime client.
 *
 * This code idea is borrowed from M. Tim Jones (IBM):
 * <http://www.ibm.com/developerworks/linux/library/l-sctp/>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/sctp.h>

static const char *progname = "daytime";

/*
 * Establish a connection to a remote SCTP server. First get the list
 * of potential network layer addresses and transport layer port
 * numbers. Iterate through the returned address list until an attempt
 * to establish a SCTP connection is successful (or no other
 * alternative exists).
 */

static int
sctp_connect(char *host, char *port)
{
    struct addrinfo hints, *ai_list, *ai;
    int n, fd = 0;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    n = getaddrinfo(host, port, &hints, &ai_list);
    if (n) {
        fprintf(stderr, "%s: getaddrinfo: %s\n",
                progname, gai_strerror(n));
        exit(EXIT_FAILURE);
    }

    for (ai = ai_list; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, IPPROTO_SCTP);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        close(fd);
    }

    freeaddrinfo(ai_list);

    if (ai == NULL) {
        fprintf(stderr, "%s: socket or connect: failed for %s port %s\n",
                progname, host, port);
        exit(EXIT_FAILURE);
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
daytime(int fd)
{
    struct sockaddr_storage peer;
    socklen_t peerlen = sizeof(peer);
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    char message[128], *p;
    ssize_t n;
    struct sctp_event_subscribe events;
    struct sctp_sndrcvinfo sinfo;
    int flags;
    
    /* Get the socket address of the remote end and convert it
     * into a human readable string (numeric format). */

    n = getpeername(fd, (struct sockaddr *) &peer, &peerlen);
    if (n) {
        fprintf(stderr, "%s: getpeername: %s\n",
                progname, strerror(errno));
        return;
    }

    n = getnameinfo((struct sockaddr *) &peer, peerlen,
                    host, sizeof(host), serv, sizeof(serv),
                    NI_NUMERICHOST | NI_NUMERICSERV);
    if (n) {
        fprintf(stderr, "%s: getnameinfo: %s\n",
                progname, gai_strerror(n));
        return;
    }

#define DAYTIME_STREAM_LOCAL 0
#define DAYTIME_STREAM_GMT 1

    /* Enable receipt of SCTP snd/rcv data via sctp_recvmsg. */

    memset(&events, 0, sizeof(events));
    events.sctp_data_io_event = 1;
    if (0 != setsockopt(fd, SOL_SCTP, SCTP_EVENTS, &events, sizeof(events))) {
	fprintf(stderr, "%s: setsockopt failed: %s\n",
		progname, strerror(errno));
    }
	
    while ((n = sctp_recvmsg(fd, message, sizeof(message) - 1,
			     NULL, 0, &sinfo, &flags)) > 0) {
        message[n] = '\0';
        p = strstr(message, "\r\n");
        if (p) *p = 0;
	switch (sinfo.sinfo_stream) {
	case DAYTIME_STREAM_LOCAL:
	    printf("%s:%s\t %s (local time)\n", host, serv, message);
	    break;
	case DAYTIME_STREAM_GMT:
	    printf("%s:%s\t %s (gmt time)\n", host, serv, message);
	    break;
	default:
	    fprintf(stderr, "%s: ignoring message from unknown stream %d\n",
		    progname, sinfo.sinfo_stream);
	    break;
	}
    }
}


int
main(int argc, char **argv)
{
    int fd;

    if (argc != 3) {
        fprintf(stderr, "usage: %s host port\n", progname);
        return EXIT_FAILURE;
    }

    fd = sctp_connect(argv[1], argv[2]);
    daytime(fd);
    sctp_close(fd);
    
    return EXIT_SUCCESS;
}
