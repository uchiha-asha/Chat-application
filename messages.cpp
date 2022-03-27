#include <stdio.h>
#include <string.h>

#define ERR_BAD_USERNAME 100
#define ERR_NOT_REGISTERED 101
#define ERR_CANT_SEND 102
#define ERR_INCOMPLETE_HEADER 103

#define USER_CLIENT 0
#define USER_SERVER 1

#define ACTION_SEND 0
#define ACTION_RECV 1
#define ACTION_FORWARD 2


int create_error_message(int error_code, char *message) {
	const char *error_message;

	if (error_code == ERR_BAD_USERNAME) 
		error_message = "Malformed username";
	else if (error_code == ERR_NOT_REGISTERED) 
		error_message = "No user registered";
	else if (error_code == ERR_CANT_SEND) 
		error_message = "Unable to send";
	else if (error_code == ERR_INCOMPLETE_HEADER)
		error_message = "Header incomplete";
	else
		return -1;

	sprintf(message, "ERROR %d %s\n\n", error_code, error_message);
	return strlen(message);
}

int create_register_message(int user, int action, const char *username, char *message) {
	if (user == USER_CLIENT)
		sprintf(message, "REGISTER ");
	else if (user == USER_SERVER)
		sprintf(message, "REGISTERED ");
	else 
		return -1;

	if (action == ACTION_SEND)
		sprintf(message + strlen(message), "TOSEND ");
	else if (action == ACTION_RECV)
		sprintf(message + strlen(message), "TORECV ");
	else
		return -1;

	sprintf(message + strlen(message), "%s\n\n", username);

	return strlen(message);
}

int create_user_message(int action, char *username, char *message, char *content) {
	if (action == ACTION_SEND) 
		sprintf(message, "SEND ");
	else if (action == ACTION_FORWARD)
		sprintf(message, "FORWARD ");
	else
		return -1;

	sprintf(message + strlen(message), "%s\nContent-length: %d\n\n%s", username, (int)strlen(content), content);

	return strlen(message);
}

int create_user_response(int action, const char *username, char *message) {
	if (action == ACTION_SEND) 
		sprintf(message, "SEND ");
	else if (action == ACTION_RECV)
		sprintf(message, "RECEIVED ");
	else
		return -1;

	sprintf(message + strlen(message), "%s\n\n", username);
	
	return strlen(message);
}

