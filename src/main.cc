#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

#include "HTTP_Server.hh"
#include "Routes.hh"
#include "Response.hh"

int main()
{
	// initiate HTTP_Server
	HTTP_Server http_server;
	init_server(&http_server, 6969);

	int client_socket;

	// registering Routes
	Router router;
	router.add("/", "index.html");
	router.add("/about", "about.html");

	//printf("\n====================================\n");
	//printf("=========ALL VAILABLE ROUTES========\n");
	// display all available routes
	router.printAll();
	// accept ->parse ->find_resources ->response loop
	while (1)
	{
		client_socket = accept(http_server.socket, NULL, NULL);
		char client_msg[4096];
		ssize_t n = read(client_socket, client_msg, sizeof(client_msg) - 1);
		if (n <= 0) return EXIT_FAILURE;
		printf("%s", client_msg);
		std::string_view request(client_msg, n);
		// extract first line
		size_t line_end = request.find("\r\n");
		if (line_end == std::string_view::npos) line_end = request.find('\n');
		std::string_view first_line = request.substr(0, line_end);

		size_t sp1 = first_line.find(' ');
		size_t sp2 = first_line.find(' ', sp1 + 1);
		std::string responseTemplate;
		// parsing client socket header to get HTTP method, route
		if (sp1 != std::string_view::npos && sp2 != std::string_view::npos) {
			std::string_view method = first_line.substr(0, sp1);
			std::string_view urlRoute = first_line.substr(sp1 + 1, sp2 - sp1 - 1);
			if (urlRoute.find("/static/") == 0) {
				//snprintf(responseTemplate, sizeof(responseTemplate), "static/index.css");
				responseTemplate = "static/index.css";
			}else {
				auto destination = router.find(std::string(urlRoute));
				responseTemplate = "templates/" + destination.value_or("404.html");
			}
		}
		// // string to token,maintain a pointer inside it
		// // extract the first line
		// std::string client_http_header = strtok(client_msg, "\n");

		// //printf("\n\n%s\n\n", client_http_header);
		// std::string_view sv = client_http_header;
		// size_t first_space = sv.find(' ');
		// std::string_view header_token = sv.substr(0, first_space);
		// //std::string header_token = strtok(client_http_header, " ");

		// int header_parse_counter = 0;

		// while (header_token != NULL)
		// {

		// 	switch (header_parse_counter)
		// 	{
		// 	case 0:
		// 		method = header_token;
		// 		break;
		// 	case 1:
		// 		urlRoute = header_token;
		// 		break;
		// 	}
		// 	header_token = strtok(NULL, " ");
		// 	header_parse_counter++;
		// }

		// char responseTemplate[100] = "";
		// // strstr finds a substr
		// if (strstr(urlRoute, "/static/") != NULL) {
		// 	snprintf(responseTemplate, sizeof(responseTemplate), "static/index.css"); 
		// }
		// else {
		// 	// Route
		// 	struct Route *destination = search(route, urlRoute);
		// 	const char *file_name = (destination == NULL) ? "404.html" : destination->renfile;
		// 	snprintf(responseTemplate, sizeof(responseTemplate), "templates/%s", file_name);
		// }
		std::string response_data = load_file_to_mem(responseTemplate);

		//std::string_view header;
		std::string header = "HTTP/1.1 200 OK\r\n\r\n"; //response_data + "\r\n\r\n";
		//send(client_socket, header.data(), header.size(), 0);
		send(client_socket, header.data(), header.size(), 0);
		send(client_socket, response_data.data(), response_data.size(), 0);
		close(client_socket);
	}
	return EXIT_SUCCESS;
}
