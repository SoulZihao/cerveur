#pragma once

typedef struct HTTP_Server {
	int socket;
	int port;
} HTTP_Server;

void init_server(HTTP_Server* http_server, int port);
