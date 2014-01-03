// server
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../protocol.h"

#include "server_io.h"
#include "server_network.h"
#include "server_reset.h"


int main(int argc, char* argv[])
{
	int status, addrlen, sockd;
	struct sockaddr_in my_name, cli_name;
/*
	FsOpenServerS server_struct;
	FsOpenServerC client_struct;

	server_struct.answer = OK;
	server_struct.server_handler = 1;
*/
	FsOpenServerS open_server_s;
	FsOpenServerC open_server_c;
	FsCloseServerS close_server_s;
	FsCloseServerC close_server_c;
	
	FsCommand command;
	FsAnswer answer;
	
	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s port_number\n", argv[0]);
		exit(1);
	}
	/* create a socket */
	sockd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockd == -1)
	{
		perror("Socket creation error");
		exit(1);
	}
	/* server address */
	my_name.sin_family = AF_INET;
	my_name.sin_addr.s_addr = INADDR_ANY;
	my_name.sin_port = htons (atoi (argv[1]));
	status = bind(sockd, (struct sockaddr*) &my_name, sizeof (my_name));

	addrlen = sizeof(struct sockaddr_in);

	status = recvfrom (sockd, (FsOpenServerC*) &open_server_c, sizeof (FsOpenServerC), 0, (struct sockaddr*) &cli_name, &addrlen);	
	status = sendto (sockd, (FsOpenServerS*) &open_server_s, sizeof (FsOpenServerS), 0, (struct sockaddr*) &cli_name, sizeof (cli_name));

	close(sockd);
	return 0;
}
