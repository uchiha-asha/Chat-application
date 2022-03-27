#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>


#include <thread>
#include <string>
#include <unordered_map>
#include <vector>
#include <signal.h>
#include "messages.cpp"


#define MAX_PACKET_SIZE 512



std::unordered_map<std::string, int> username_to_socket[2];
std::vector<std::thread> client_threads;
int socket_listen;

bool username_exist(std::string username, int action) {
	return username_to_socket[action].find(username) == username_to_socket[action].end();
}

bool bad_username(std::string username) {
	for (auto c : username) {
		if (!((c>='0' && c<='9') || (c>='a' && c<='z') || (c>='A' && c<='Z')))
			return true;
	}
	return false || (username == "ALL");
}

void close_connection(std::string username) {
	close(username_to_socket[0][username]);
	close(username_to_socket[1][username]);
	username_to_socket[0].erase(username_to_socket[0].find(username));
	username_to_socket[1].erase(username_to_socket[1].find(username));
	printf("%s is gone\n", username.c_str());
}

void catch_ctrl_c(int signal) {
	for (auto tmp : username_to_socket[0])
		close_connection(tmp.first);

	close(socket_listen);

	for (int i=0; i<client_threads.size(); i++) {
		client_threads[i].detach();
	}

	exit(signal);
}

void forward_to_client(char *username, char *message, const char *recipient, char *message_response) {
	char forward_message[MAX_PACKET_SIZE];
	create_user_message(ACTION_FORWARD, username, forward_message, message);
	int	socket_recipient_recv = username_to_socket[ACTION_RECV][recipient];

	printf("%s\n", forward_message);
	if (send(socket_recipient_recv, forward_message, strlen(forward_message), 0) < 0) {
		printf("%s disconnected\n", recipient);
		create_error_message(ERR_CANT_SEND, message_response);
	}
	else {
		char recipient_response[MAX_PACKET_SIZE];
		int bytes_received = recv(socket_recipient_recv, recipient_response, MAX_PACKET_SIZE, 0);
		if (bytes_received < 1) {
			create_error_message(ERR_CANT_SEND, message_response);
		}
		else {
			recipient_response[bytes_received] = '\0';
			printf("%s", recipient_response);
			if (strstr(recipient_response, "ERROR") != NULL) {
				create_error_message(ERR_CANT_SEND, message_response);
			}
			else{
				create_user_response(ACTION_SEND, recipient, message_response);
			}
		}
	}
}

void handle_client(int socket_client) {
	printf("Handling client on socket %d\n", socket_client);
	char register_request[MAX_PACKET_SIZE];

	int bytes_received = recv(socket_client, register_request, MAX_PACKET_SIZE, 0);
	if (bytes_received < 1) {
		perror("Client disconnected...\n");
		return;
	}

	register_request[bytes_received] = '\0';
	printf("request: %s", register_request);

	int action = ACTION_SEND;
	if (strstr(register_request, "TORECV") != NULL) 
		action = ACTION_RECV;

	char username[MAX_PACKET_SIZE];
	if (action == ACTION_SEND)
		sscanf(register_request, "REGISTER TOSEND %s\n\n", username);
	else
		sscanf(register_request, "REGISTER TORECV %s\n\n", username);

	char register_response[512];
	if (bad_username(username) || !username_exist(username, action)) {
		create_error_message(ERR_BAD_USERNAME, register_response);
	}
	else if (username_exist(username, action)) {
		create_register_message(USER_SERVER, action, username, register_response);
		username_to_socket[action][username] = socket_client;
	}
	else {
		create_error_message(ERR_NOT_REGISTERED, register_response);
	}

	printf("response: %s", register_response);
	if (send(socket_client, register_response, strlen(register_response), 0) < 0) {
		perror("Client disconnected\n");
		return;
	}

	if (action == ACTION_RECV) {
		return;
	}

	while (true) {
		char message_request[MAX_PACKET_SIZE];
		bytes_received = recv(socket_client, message_request, MAX_PACKET_SIZE, 0);
		if (bytes_received < 1) {
			continue;
		}
		message_request[bytes_received] = '\0';

		if (strcmp(message_request, "#exit") == 0) {
			close_connection(username);
			return;
		}
		
		printf("%s\n", message_request);


		char message[MAX_PACKET_SIZE], recipient[MAX_PACKET_SIZE];
		int content_length = 0;
		sscanf(message_request, "SEND %s\nContent-length: %d\n\n%[^\n]", recipient, &content_length, message);

		if (content_length != (int)strlen(message)) {
			char message_response[MAX_PACKET_SIZE];
			create_error_message(ERR_INCOMPLETE_HEADER, message_response);
			printf("%s", message_response);
			if (send(socket_client, message_response, strlen(message_response), 0) < 0) {
				close_connection(username);
				return;
			}
			close_connection(username);
			return;
		} 

		

		if (strcmp(recipient, "ALL") == 0) {
			bool success = true;
			for (auto tmp : username_to_socket[0]) {
				if (strcmp(username, tmp.first.c_str()) == 0) continue;
				char message_response[MAX_PACKET_SIZE];
				forward_to_client(username, message, tmp.first.c_str(), message_response);
				if (strstr(message_response, "ERROR") != NULL) {
					success = false;
				}
			}
			char message_response[MAX_PACKET_SIZE];
			if (success)
				create_user_response(ACTION_SEND, recipient, message_response);
			else
				create_error_message(ERR_CANT_SEND,message_response);

			printf("%s", message_response);
			if (send(socket_client, message_response, strlen(message_response), 0) < 0) {
				close_connection(username);
				return;
			}
			continue;
		}

		char message_response[MAX_PACKET_SIZE];
		if (username_exist(recipient, ACTION_RECV) || username_exist(recipient, ACTION_SEND)) {
			create_error_message(ERR_CANT_SEND, message_response);
		}
		else {
			forward_to_client(username, message, recipient, message_response);
		}
		printf("%s", message_response);
		if (send(socket_client, message_response, strlen(message_response), 0) < 0) {
			close_connection(username);
			return;
		}
	}


}

int configure(int &socket_listen, char *port) {
	printf("Configuring local address....\n");
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	addrinfo *bind_addresss;
	getaddrinfo(0, port, &hints, &bind_addresss);

	printf("Creating socket....\n");
	socket_listen = socket(bind_addresss->ai_family, bind_addresss->ai_socktype, bind_addresss->ai_protocol);

	if (socket_listen < 0) {
		perror("socket() failed\n");
		return -1;
	}

	printf("Binding socket to local address....\n");
	if (bind(socket_listen, bind_addresss->ai_addr, bind_addresss->ai_addrlen)) {
		perror("bind() failed\n");
		return -1;
	}

	freeaddrinfo(bind_addresss);
	return 0;
}



int main(int argc, char** argv) {

	if (configure(socket_listen, argv[1]) < 0) {
		return 0;
	}
	
	printf("Listening...\n");
	if (listen(socket_listen, 50) < 0) {
		perror("listen() failed\n");
		return 0;
	}
	
	signal(SIGINT, catch_ctrl_c);
	while (true) {
		
		sockaddr_storage client_address;
		socklen_t client_len = sizeof(client_address);
		int socket_client = accept(socket_listen, (sockaddr*)&client_address, &client_len);
		if (socket_client < 0) {
			continue;
		} 
		std::thread t(handle_client, socket_client);
		client_threads.push_back(move(t));
		
	}

	for (int i=0; i<client_threads.size(); i++) {
		if (client_threads[i].joinable())
			client_threads[i].join();
	}

	
}


