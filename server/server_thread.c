#include "server_thread.h"

void *server_thread_function(void *parameters) {
    struct IncomingRequest *inc_request = (struct IncomingRequest *) parameters;

    switch(inc_request->request.command) {
        case OPEN_SERVER:
            s_open_server(inc_request);
            break;

        case CLOSE_SERVER:
            s_close_server(inc_request);
            // @todo tymczasowe
            // zamykamy server, trzeba zrobic bardziej praktycznie, macie jakis pomysl?
            // zamykanie serwera przez klienta raczej nie wchodzi w gre, chyba ze jakis klient sterujacy serwerem
            break;

        case OPEN:
            s_open(inc_request);
            break;

        case READ:
            s_read(inc_request);
            break;

        case WRITE:
            s_write(inc_request);
            break;

        case LSEEK:
            s_lseek(inc_request);
            break;

        case FSTAT:
            s_fstat(inc_request);
            break;

        case CLOSE:
            s_close(inc_request);
            break;

        case LOCK:
            s_lock(inc_request);
            break;
    }

    pthread_exit(NULL);
    return NULL;
}
