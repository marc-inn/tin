#include "../protocol.h"
#include "server_log.h"
#include "server_const.h"
#include "server_io.h"
#include "server_network.h"
#include "server_synchroniser.h"
#include <stdio.h>

int s_open(IncomingRequest *inc_request) {
    FsResponse response;
    FsOpenC data_c = inc_request->request.data.open;
    int lock_result;
    int server_handler = data_c.server_handler;

    if (session_check_if_exist(server_handler) == -1)
    {
        response.answer= EC_SESSION_TIMED_OUT;
        response.data.open.status = -1;
        VDP1 ("Session timed out with: %d\n", server_handler);
        status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
        return -1;
    }

    char *file_name = (char *) calloc(data_c.name_len, sizeof(char));
    strncpy(file_name, data_c.name, data_c.name_len);

    VDP3("Incoming OPEN request: %s, len = %zu (flags: %d)\n", file_name, data_c.name_len, data_c.flags);

    if (inc_request->request.data.open.flags == O_RDONLY)
        lock_result = session_lock_file(inc_request->request.data.open.server_handler, file_name, FLOCK_SH);
    else
        lock_result = session_lock_file(inc_request->request.data.open.server_handler, file_name, FLOCK_EX);

    if(lock_result < 0) {
        /* Nie udalo sie dostac blokady na plik - trzeba powiadomic o tym klienta. */
        VDP0("Lock failed, request turned down.\n");
        response.data.open.status = -1;
        response.answer = EF_FILE_BLOCKED;

    } else {
        VDP0("Lock accepted, request accepted.\n");
        VDP2("Attempting to open %s file in %d mode...\n", file_name, data_c.flags);

        int fh = open(file_name, data_c.flags);

        if(fh == -1) {

            VDP1("File %s cannot be opened.\n", file_name);
            session_unlock_file(inc_request->request.data.open.server_handler, lock_result);
            response.data.open.status = -1;
            response.answer = EF_NOT_FOUND;
            response.data.open.fd = -1;
        } else {

            VDP1("File %s opened successfully. \n", file_name);
            status = session_set(inc_request->request.data.open.server_handler, lock_result, fh);
            response.data.open.status = lock_result;
            response.answer = IF_OK;
            response.data.open.fd = lock_result;
        }
    }
    free(file_name);
    return sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
}

int s_write (IncomingRequest *inc_request)
{
    FsResponse response;
    FsWriteC data_c = inc_request->request.data.write;

    int status = 0;
    size_t parts_number = data_c.parts_number;
    int server_handler = data_c.server_handler;
    int fd = data_c.fd;
    int part_id = data_c.part_id;

    SessionFileBuffer* session_buffer;

    if (session_check_if_exist(server_handler) == -1)
    {
        response.answer= EC_SESSION_TIMED_OUT;
        response.data.write.status = -1;
        VDP1 ("Session timed out with: %d\n", server_handler);
        status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
        return -1;
    }

    switch (inc_request->request.command)
    {
        case WRITE:
            VDP1("Session %d allowed to receive file\n", server_handler);
            session_buffer = session_get_buffer (server_handler, fd);
            session_buffer->received_parts = (int*) calloc(parts_number, sizeof(int));
            session_buffer->buffer = (char*) calloc(data_c.buffer_len, sizeof(char));
            session_buffer->file_size = data_c.buffer_len;

            for (int i = 0; i<data_c.buffer_len; i++)
            {
                char a = '-';
                strncpy(session_buffer->buffer+i, &a , 1);
            }
            response.answer = IF_CONTINUE;
            status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
            // errors
            return 0;

        case WRITE_PACKAGES:
            session_buffer = session_get_buffer (server_handler, fd);
            if ( session_buffer == NULL )
            {
                response.answer = EF_ACCESS_ERROR;
                break;
            }
            VDP3("Session %d received part %d/%zu\n", server_handler, part_id, parts_number);
            strncpy(session_buffer->buffer + data_c.part_id * BUF_LEN, data_c.buffer, data_c.buffer_len);
            int *current_package = session_buffer->received_parts + data_c.part_id;
            *current_package = 1;
            return 0;

        case WRITE_ALL:
            session_buffer = session_get_buffer (server_handler, fd);
            if (session_buffer == NULL)
            {
                response.answer = EF_ACCESS_ERROR;
                response.data.write.status = -1;
                status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
                break;
            }
            response.answer = IF_OK;
            for(int i=0; i<parts_number; ++i)
            {
                int* current = session_buffer->received_parts + i;
                if (*current == 0)
                {
                    response.answer = EF_CORRUPT_PACKAGE;
                    response.data.write.status = -1;
					printf("Corrupt package\n");
                    break;
                }

            }
            if (response.answer == IF_OK)
            {
                int file = session_get (server_handler, fd);
                if (file == -1)
                {
                     VDP0 ("Could not get file descriptor in write\n");
                } else
                {
                    size_t count = write(file, session_buffer->buffer, session_buffer->file_size);
                    response.data.write.status = count;
                    if (count <= 0) response.answer = EF_NOT_FOUND;
                }
            }
            if (session_buffer!=NULL) free(session_buffer->received_parts);
            if (session_buffer!=NULL) free(session_buffer->buffer);
            session_buffer->received_parts = NULL;
            session_buffer->buffer = NULL;
            status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
            break;

        default:
            break;
    }

    return 0;
}

int s_close (IncomingRequest *inc_request)
{
    FsResponse response;

    FsCloseC data_c = inc_request->request.data.close;
    int server_handler = data_c.server_handler;
    int fd = data_c.fd;

    VDP0 ("Closing operation\n");

    if (session_check_if_exist(server_handler) == -1)
    {
        response.answer= EC_SESSION_TIMED_OUT;
        response.data.close.status = -1;
        VDP1 ("Session timed out with: %d\n", server_handler);
        status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
        return -1;
    }

    int file = session_get (server_handler, fd);

    if (file == -1)
    {
        response.data.close.status = -1;
        VDP0 ("Could not get file descriptor in close\n");
    }
    else
    {
        response.data.close.status = close(file);
        session_unlock_file(server_handler, fd);
        response.answer = IF_OK;
        VDP2 ("Closing file with fd: %d with status %d\n", fd, response.data.close.status);
    }

    return sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
}

int s_read (IncomingRequest *inc_request)
{
    FsResponse response;
    FsReadC data_c = inc_request->request.data.read;
    int server_handler = data_c.server_handler;
    int buffer_len = data_c.buffer_len;
    size_t parts = buffer_len/BUF_LEN;
    size_t last_part = buffer_len%BUF_LEN;
    int i = 0;
    int fd = data_c.fd;
    void* buf;
    off_t offset = 0;
    struct stat stat_of_file;

    if (session_check_if_exist(server_handler) == -1)
    {
        response.answer= EC_SESSION_TIMED_OUT;
        response.data.read.status = -1;
        VDP1 ("Session timed out with: %d\n", server_handler);
        status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
        return -1;
    }

    int file = session_get (server_handler, fd);

    if (file == -1)
    {
        response.data.read.status = -1;
        VDP0 ("Could not get file descriptor in read\n");
    }
    else
    {
        offset = session_get_offset (server_handler, fd);
        status = fstat (file, &stat_of_file);
        if (offset < 0 || status < 0)
        {
            response.answer = EF_NOT_FOUND;
            status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
            return -1;
        }

        size_t file_size = stat_of_file.st_size;
        size_t size_to_end = file_size - offset;

        if (buffer_len > size_to_end)
        {
            buffer_len = size_to_end;
        }

        if (buffer_len <= 0)
        {
            VDP0 ("To small buffer length in read\n");
            response.answer = EF_BAD_REQUEST;
            response.data.read.status = -1;
            response.data.read.buffer_len = buffer_len;
            status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
            return -1;
        }

        response.answer = IF_CONTINUE;
        response.data.read.buffer_len = buffer_len;
        response.data.read.parts_number = parts;
        status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);

        buf = (char*) malloc(buffer_len);
        response.data.read.status = read(file, buf, buffer_len);

        parts = buffer_len/BUF_LEN;
        last_part = buffer_len%BUF_LEN;

        if (response.data.read.status <= 0)
        {
            free(buf);
            response.answer = EF_NOT_FOUND;
            VDP0 ("Could not read file in read\n");
            response.data.read.status = -1;
            status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
            return -1;
        }

        for (i=0; i<parts; i++)
        {
            strncpy (response.data.read.buffer, buf + i * BUF_LEN, BUF_LEN);
            response.data.read.part_id = i;
            response.data.read.buffer_len = BUF_LEN;
            status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
        }

        if (last_part != 0)
        {
            response.data.read.buffer_len = last_part;
            response.data.read.part_id = i;
            strncpy (response.data.read.buffer, buf + i * BUF_LEN, last_part);
            status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
        }

        free(buf);
        usleep(500);
        response.answer = IF_OK;
        status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
    }

    return 0;
}

int s_fstat (IncomingRequest *inc_request)
{
    FsResponse response;
    FsFstatC data_c = inc_request->request.data.fstat;
    int server_handler = data_c.server_handler;
    int fd = data_c.fd;

    if (session_check_if_exist(server_handler) == -1)
    {
        response.answer= EC_SESSION_TIMED_OUT;
        response.data.fstat.status = -1;
        status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
        return -1;
    }

    int file = session_get (server_handler, fd);

    if (file == -1)
    {
        response.data.fstat.status = -1;
        VDP0 ("Could not get file descriptor in fstat\n");
    }
    else
    {
        struct stat* new_stat = (struct stat*) malloc(sizeof(struct stat));
        response.data.fstat.status = fstat (file, new_stat);
        response.data.fstat.stat.mode = new_stat->st_mode;
        response.data.fstat.stat.size = new_stat->st_size;
        response.data.fstat.stat.atime = new_stat->st_atime;
        response.data.fstat.stat.mtime = new_stat->st_mtime;
        response.data.fstat.stat.ctime = new_stat->st_ctime;
        status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
        free(new_stat);
    }
    return 0;
}

int s_lock (IncomingRequest *inc_request)
{
    FsResponse response;
    FsLockC data_c = inc_request->request.data.lock;
    int server_handler = data_c.server_handler;
    int fd = data_c.fd;

    if (session_check_if_exist(server_handler) == -1)
    {
        response.answer= EC_SESSION_TIMED_OUT;
        response.data.lock.status = -1;
        status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
        return -1;
    }

    int file = session_get (server_handler, fd);

    if (file == -1)
    {
        VDP0 ("Could not get file descriptor in lock\n");
        response.data.lock.status = -1;
    }
    else
    {
        // @todo ustawiamy flocka jedynie gdy chcemy czytac cos po kryjomu
        response.data.lock.status = flock(file, data_c.mode);
    }

    return sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
}

int s_lseek (IncomingRequest *inc_request)
{
    FsResponse response;
    FsLseekC data_c = inc_request->request.data.lseek;
    int server_handler = data_c.server_handler;
    int fd = data_c.fd;

    if (session_check_if_exist(server_handler) == -1)
    {
        response.answer= EC_SESSION_TIMED_OUT;
        response.data.lseek.status = -1;
        status = sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
        return -1;
    }

    int file = session_get (server_handler, fd);

    if (file == -1)
    {
        VDP0 ("Could not get file descriptor in lseek\n");
        response.data.lseek.status = -1;
    }
    else
    {
        off_t offset = lseek (file, data_c.offset, data_c.whence);
        if (offset < 0) offset = 0;
        off_t status = session_set_offset (server_handler, fd, offset);
        if (status < 0) response.answer = EF_ACCESS_ERROR;
        response.data.lseek.status = status;
    }

    return sendto(sockd, &response, sizeof(FsResponse), 0,(struct sockaddr*) &(inc_request->client_addr), inc_request->client_addr_len);
}
