#include "tin_library.h"
#include <errno.h>
#include <curses.h>

/**
 * UDP jest protokolem bezpolaczeniowym,
 * wiec ta funkcja sluzy nam
 * do zarejestrowania klienta w serwerze
 *
 * wysylamy login a serwer jesli odpowie dobrze to zwracamy uchwyt
 */

#define WAIT_TO_STOP_RCV 1
#define WAIT_TO_SEND 1
#define WAIT_TO_RCV 1

const int ANS_X = 22;
const int ANS_Y = 1;

int sockd;
int port;
struct sockaddr_in my_addr;
struct sockaddr_in srv_addr;

int fs_open_server (const char* server_address, int server_port)
{
    FsResponse response;
    FsRequest request;
    request.command = OPEN_SERVER;
    socklen_t addrlen = sizeof(struct sockaddr_in);
	int status, count;

	sockd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockd == -1)
    {
        mvprintw(ANS_X, ANS_Y, "Socket creation error");
        return -1;
    }

    /* client address */
	my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = 0;

    status = bind (sockd, (struct sockaddr*)&my_addr, sizeof(my_addr));

    if (status != 0)
    {
        close(sockd);
        sockd = -1;
        mvprintw(ANS_X, ANS_Y, "Socket binding error");
        return -1;
    }

  	/* server address */
	srv_addr.sin_family = AF_INET;
	inet_aton (server_address, &srv_addr.sin_addr);
    srv_addr.sin_addr.s_addr = inet_addr(server_address);
    port = server_port;
    srv_addr.sin_port = htons(port);

    struct timeval time_val;

    time_val.tv_sec = WAIT_TO_STOP_RCV;
    time_val.tv_usec = 0;

    setsockopt(sockd, SOL_SOCKET, SO_RCVTIMEO, (char*) &time_val, sizeof(struct timeval));

    status = connect (sockd, (struct sockaddr*) &srv_addr, sizeof(srv_addr));

    status = send(sockd, &request, sizeof(FsRequest), 0);
    count = recv(sockd, &response, sizeof(FsResponse), 0);

    info (response.answer);
    return response.data.open_server.server_handler;
}

int fs_close_server (int server_handler)
{
    if (server_handler < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect server handler");
        return -1;
    }

    FsResponse response;

    FsRequest request;
    request.command = CLOSE_SERVER;
    request.data.close_server.server_handler = server_handler;

	int status, count;

    if (sockd == -1)
    {
        mvprintw(ANS_X, ANS_Y, "Socket is closed");
        return -1;
    }

    status = send(sockd, &request, sizeof(FsRequest), 0);
	count = recv(sockd, &response, sizeof(FsResponse), 0);

    info (response.answer);
	return response.data.close_server.status;
}

int fs_open (int server_handler, const char* name, int flags)
{
    if (server_handler < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect server handler");
        return -1;
    }

    FsResponse response;

    FsRequest request;
    request.command = OPEN;
    request.data.open.server_handler = server_handler;
    strncpy (request.data.open.name, name, strlen(name));
    request.data.open.name_len = strlen(name);
    request.data.open.flags = flags;
    int status, count;

    if (sockd == -1)
    {
        mvprintw(ANS_X, ANS_Y, "Socket is closed");
        return -1;
    }

	status = send(sockd, &request, sizeof(FsRequest), 0);
	count = recv(sockd, &response, sizeof(FsResponse), 0);

    info(response.answer);
	return response.data.open.fd;
}

int fs_write (int server_handler, int fd, const void *buf, size_t len)
{
    if (server_handler < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect server handler");
        return -1;
    }

    if (len <= 0)
    {
        mvprintw(ANS_X, ANS_Y, "Length of buffer is too small");
        return -1;
    }

    if (fd < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect file descriptor");
        return -1;
    }

 	int i=0, status=0;
    size_t count = 0;;
    size_t parts = (len-1)/BUF_LEN;
    size_t last_part = len%BUF_LEN;

    FsResponse response;

    FsRequest request;
    request.command = WRITE;
    request.data.write.server_handler = server_handler;
    request.data.write.fd = fd;
    request.data.write.buffer_len = len;
    request.data.write.parts_number = parts+1;

    if (sockd == -1)
    {
        mvprintw(ANS_X, ANS_Y, "Socket is closed");
        return -1;
    }

    status = send (sockd, &request, sizeof(FsRequest), 0);
    count = recv (sockd, &response, sizeof(FsResponse), 0);

    if (info(response.answer) == -1) return response.data.write.status;

    if (response.answer == IF_CONTINUE)
    {
        request.command = WRITE_PACKAGES;
        for(i=0; i<parts; i++)
        {
            strncpy (request.data.write.buffer, buf + i * BUF_LEN, BUF_LEN);
            request.data.write.part_id = i;
            request.data.write.buffer_len = BUF_LEN;
            status = send (sockd, &request, sizeof(FsRequest), 0);
        }
        if (last_part == 0) last_part = BUF_LEN;
        request.data.write.buffer_len = last_part;
        strncpy (request.data.write.buffer, buf + i * BUF_LEN, last_part);
        request.data.write.part_id = i;
        status = send (sockd, &request, sizeof(FsRequest), 0);

        sleep(WAIT_TO_SEND);

        request.command = WRITE_ALL;
        status = send (sockd, &request, sizeof(FsRequest), 0);
        count = recv (sockd, &response, sizeof(FsResponse), 0);

    }
    info(response.answer);
    return response.data.write.status;
}

int fs_read (int server_handler, int fd, void *buf, size_t len)
{
    if (server_handler < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect server handler");
        return -1;
    }

    if (len <= 0)
    {
        mvprintw(ANS_X, ANS_Y, "Length of buffer is too small");
        return -1;
    }

    if (fd < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect file descriptor");
        return -1;
    }

    int status=0;
    size_t count = 0;
    FsResponse response;

    FsRequest request;
    request.command = READ;
    request.data.read.server_handler = server_handler;
    request.data.read.fd = fd;
    request.data.read.buffer_len = len;

	if (sockd == -1)
    {
        mvprintw(ANS_X, ANS_Y, "Socket is closed");
        return -1;
    }

    status = send(sockd, &request, sizeof(FsRequest), 0);
    count = recv(sockd, &response, sizeof(FsResponse), 0);

    if (info(response.answer) < 0) return -1;

    size_t parts = response.data.read.parts_number;
    size_t file_size = response.data.read.buffer_len;

    int* received_parts = (int*) calloc(parts, sizeof(int));

    for(int i=0; i<parts; ++i)
    {
        count = recv(sockd, &response, sizeof(FsResponse), 0);
        if (response.data.read.status <= 0) return -1;
        strncpy (buf + response.data.read.part_id * BUF_LEN, response.data.read.buffer, response.data.read.buffer_len);
        int *current_index = received_parts + i;
        *current_index = 1;
    }

    // sleep (WAIT_TO_RCV);
    count = recv(sockd, &response, sizeof(FsResponse), 0);

    for(int i=0; i<parts; ++i)
    {
        int* current_index = received_parts + i;
        if (*current_index == 0)
        {
            response.answer = EF_CORRUPT_PACKAGE;
            break;
        }
    }

    free(received_parts);
    info(response.answer);
	return response.data.read.status;
}

int fs_lseek (int server_handler, int fd, long offset, int whence)
{
    if (server_handler < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect server handler in lseek");
        return -1;
    }
    if (fd < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect file descriptor in lseek");
        return -1;
    }

    FsResponse response;

    FsRequest request;
    request.command = LSEEK;
    request.data.lseek.server_handler = server_handler;
    request.data.lseek.fd = fd;
    request.data.lseek.offset = offset;
    request.data.lseek.whence = whence;

	int status, count;

    if (sockd == -1)
    {
        mvprintw(ANS_X, ANS_Y, "Socket is closed");
        return -1;
    }

	status = send(sockd, &request, sizeof(FsRequest), 0);
	count = recv(sockd, &response, sizeof(FsResponse), 0);

    info(response.answer);
	return response.data.lseek.status;
}

int fs_close (int server_handler, int fd)
{
    if (server_handler < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect server handler in close");
        return -1;
    }
    if (fd < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect file descriptor in close");
        return -1;
    }

    FsResponse response;

    FsRequest request;
    request.command = CLOSE;
    request.data.close.server_handler = server_handler;
    request.data.close.fd = fd;

	int status, count;

    if (sockd == -1)
    {
        mvprintw(ANS_X, ANS_Y, "Socket is closed");
        return -1;
    }

	status = send(sockd, &request, sizeof(FsRequest), 0);
	count = recv(sockd, &response, sizeof(FsResponse), 0);

    info(response.answer);
	return response.data.close.status;
}

int fs_lock (int server_handler, int fd, int mode)
{
    if (server_handler < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect server handler");
        return -1;
    }

    if (fd < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect file descriptor");
        return -1;
    }

    FsResponse response;

    FsRequest request;
    request.command = LOCK;
    request.data.lock.server_handler = server_handler;
    request.data.lock.fd = fd;
    request.data.lock.mode = mode;

	int status, count;

    if (sockd == -1)
    {
        mvprintw(ANS_X, ANS_Y, "Socket is closed");
        return -1;
    }

	status = send(sockd, &request, sizeof(FsRequest), 0);
	count = recv(sockd, &response, sizeof(FsResponse), 0);

    if (response.answer != IF_OK)
    {
        mvprintw(ANS_X, ANS_Y, "Receiving packets error");
        return -1;
    }

    info(response.answer);
	return response.data.lock.status;
}

int fs_fstat (int server_handler, int fd, struct stat* buf)
{
    if (server_handler < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect server handler");
        return -1;
    }

   if (fd < 0)
    {
        mvprintw(ANS_X, ANS_Y, "Incorrect file descriptor");
        return -1;
    }

    FsResponse response;

    FsRequest request;
    request.command = FSTAT;
    request.data.fstat.server_handler = server_handler;
    request.data.fstat.fd = fd;
	int status, count;

    if (sockd == -1)
    {
        mvprintw(ANS_X, ANS_Y, "Socket is closed");
        return -1;
    }

	status = send(sockd, &request, sizeof(FsRequest), 0);
	count = recv(sockd, &response, sizeof(FsResponse), 0);

    info(response.answer);

    buf->st_mode = response.data.fstat.stat.mode;
    buf->st_size = response.data.fstat.stat.size;
    buf->st_atime = response.data.fstat.stat.atime;
    buf->st_mtime = response.data.fstat.stat.mtime;
    buf->st_ctime = response.data.fstat.stat.ctime;

    return response.data.fstat.status;
}

int info (FsAnswer answer)
{
    switch (answer)
    {
        case IF_OK:
            mvprintw(ANS_X, ANS_Y, "File operation ended with success\n");
            break;

        case EF_CORRUPT_PACKAGE:
            mvprintw(ANS_X, ANS_Y, "File not send, corrupt package, check your file descriptor or server handler");
            return -1;

        case EF_BAD_REQUEST:
            mvprintw(ANS_X, ANS_Y, "Check request parameters");
            return -1;

        case EF_NOT_FOUND:
            mvprintw(ANS_X, ANS_Y, "File not found on server");
            return -1;

        case EC_SESSION_TIMED_OUT:
            close(sockd);
            sockd = -1;
            mvprintw(ANS_X, ANS_Y, "Session with server timed out");
            return -1;

        default:
            break;
    }
    return 0;
}
