#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#include <thread>
#include <string>
#include <signal.h>
#include <stdlib.h>
#include "messages.cpp"

#define MAX_PACKET_SIZE 512
#define RECV_TIMEOUT 1
#define ANSI_COLOR_RESET   "\x1b[0m"


std::string username;
int socket_send, socket_recv;
std::thread t_send, t_recv;
char colors[6][100]={"\033[31m", "\033[32m", "\033[33m", "\033[34m", "\033[35m","\033[36m"};


int create_socket(addrinfo *peer_address) {
	int socket_peer;
	socket_peer = socket(peer_address->ai_family, peer_address->ai_socktype, peer_address->ai_protocol);

	if (socket_peer < 0) {
		perror("socket() failed\n");
		return -1;
	}

	return socket_peer;
}

void catch_ctrl_c(int signal) 
{
	char str[MAX_PACKET_SIZE]="#exit";
	send(socket_send,str,sizeof(str),0);
	
	t_send.detach();
	t_recv.detach();
	close(socket_send);
	close(socket_recv);
	exit(signal);
}

int connect_to_server(char *host, char *port) {

	printf("Configuring remote address....\n");
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	addrinfo *peer_address;
	if (getaddrinfo(host, port, &hints, &peer_address)) {
		perror("getaddrinfo() failed\n");
		return -1;
	}

	printf("Creating sockets....\n");
	socket_send = create_socket(peer_address), socket_recv = create_socket(peer_address);

	if (socket_send < 0 || socket_recv < 0) {
		return -1;
	}


	printf("Connecting....\n");
	if (connect(socket_send, peer_address->ai_addr, peer_address->ai_addrlen)) {
		perror("connect() failed\n");
		return -1;
	}

	if (connect(socket_recv, peer_address->ai_addr, peer_address->ai_addrlen)) {
		perror("connect() failed\n");
		return -1;
	}

	freeaddrinfo(peer_address);
	printf("Connected\n");
	return 0;
}



void handle_send() {
	printf("Handling send socket %d\n", socket_send);
	char register_request[MAX_PACKET_SIZE], register_response[MAX_PACKET_SIZE];
	create_register_message(USER_CLIENT, ACTION_SEND, username.c_str(), register_request);

	if (send(socket_send, register_request, strlen(register_request), 0) < 0) {
		perror("Server error...please try again\n");
		return;
	}

	int bytes_received = recv(socket_send, register_response, MAX_PACKET_SIZE, 0);
	if (bytes_received < 1) {
		printf("Receive failed...please try again\n");
		return;
	}
	register_response[bytes_received] = '\0';
	printf("%s", register_response);

	if (strstr(register_response, "ERROR") != NULL) {
		return;
	}

	while (true) {
		char line[MAX_PACKET_SIZE], message[MAX_PACKET_SIZE], recipient[MAX_PACKET_SIZE], send_message[MAX_PACKET_SIZE];
		if (fgets(line, sizeof(line), stdin) == 0)
			continue;
		if (line[0] != '@') {
			printf("bad message\n");
			continue;
		}
		sscanf(line, "@%s %[^\n]", recipient, message);
		create_user_message(ACTION_SEND, recipient, send_message, message);
		
		if (send(socket_send, send_message, strlen(send_message), 0) < 0) {
			perror("Server error...\n");
			return;
		}

		char send_message_response[MAX_PACKET_SIZE];
		int bytes_received = recv(socket_send, send_message_response, MAX_PACKET_SIZE, 0);
		if (bytes_received < 1) {
			printf("Receive failed...\n");
			return;
		}
		send_message_response[bytes_received] = '\0';
		printf("%s", send_message_response);

	}
}

void handle_recv() {
	printf("Handling recv socket %d\n", socket_recv);
	char register_request[MAX_PACKET_SIZE], register_response[MAX_PACKET_SIZE];
	create_register_message(USER_CLIENT, ACTION_RECV, username.c_str(), register_request);

	if (send(socket_recv, register_request, strlen(register_request), 0) < 0) {
		perror("Server error...please try again\n");
		return;
	}

	int bytes_received = recv(socket_recv, register_response, MAX_PACKET_SIZE, 0);
	if (bytes_received < 1) {
		printf("Receive failed...please try again\n");
		return;
	}
	register_response[bytes_received] = '\0';
	printf("%s", register_response);

	if (strstr(register_response, "ERROR") != NULL) {
		return;
	}

	while (true) {
		char incoming_message[MAX_PACKET_SIZE];
		bytes_received = recv(socket_recv, incoming_message, MAX_PACKET_SIZE, 0);
		if (bytes_received < 0) {
			perror("Server error...\n");
			return;
		}

		incoming_message[bytes_received] = '\0';
		char message[MAX_PACKET_SIZE], sender[MAX_PACKET_SIZE];
		int content_length = 0;
		sscanf(incoming_message, "FORWARD %s\nContent-length: %d\n\n%[^\n]", sender, &content_length, message);

		char message_response[MAX_PACKET_SIZE];
		if (content_length != (int)strlen(message)) {
			create_error_message(ERR_INCOMPLETE_HEADER, message_response);
		}
		else {
			printf("%sFrom %s: %s%s\n", colors[rand()%6], sender, message, ANSI_COLOR_RESET);
			create_user_response(ACTION_RECV, username.c_str(), message_response);
		}

		if (send(socket_recv, message_response, strlen(message_response), 0) < 0) {
			perror("Server error...\n");
			return;
		}

		
		if (strstr(message_response, "ERROR") != NULL) {
			catch_ctrl_c(SIGINT);
			return;
		}

	}
}



int main(int argc, char *argv[]) {

	
	if (connect_to_server(argv[1], argv[2]) < 0) {
		return 0;
	}
	srand(time(0));
	username = argv[3];
	
	std::thread t1(handle_send), t2(handle_recv);
	t_send = move(t1), t_recv = move(t2);
	signal(SIGINT, catch_ctrl_c);

	t_send.join();
	t_recv.join();
	
}