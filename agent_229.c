#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9410
#define BUFFER_SIZE 1024

#define AUTH_TOKEN "OPS-2229"
#define SESSION_ID "9222"

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    char buffer[BUFFER_SIZE];

    /* Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    printf("TCP socket created successfully.\n");

    /* Prepare server address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    /* Bind socket to port */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    printf("Agent bound to port %d.\n", PORT);

    /* Listen for connections */
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Agent is listening...\n");

    /* Accept Controller connection */
    client_fd = accept(server_fd,
                       (struct sockaddr *)&client_addr,
                       &client_len);

    if (client_fd < 0)
    {
        perror("accept");
        close(server_fd);
        return 1;
    }

    printf("Controller connected successfully!\n");

    /*
     * Receive authentication command
     */
    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received = recv(client_fd,
                                  buffer,
                                  sizeof(buffer) - 1,
                                  0);

    if (bytes_received < 0)
    {
        perror("recv");
        close(client_fd);
        close(server_fd);
        return 1;
    }

    buffer[bytes_received] = '\0';

    printf("Received: %s", buffer);

    /*
     * Check authentication command
     */
    char expected_command[BUFFER_SIZE];

    snprintf(expected_command,
             sizeof(expected_command),
             "AUTH %s\n",
             AUTH_TOKEN);

    if (strcmp(buffer, expected_command) == 0)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "OK AUTHENTICATED SID:%s\n",
                 SESSION_ID);

        send(client_fd,
             response,
             strlen(response),
             0);

        printf("Authentication successful.\n");
    }
    else
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 001 AUTH_FAILED SID:%s\n",
                 SESSION_ID);

        send(client_fd,
             response,
             strlen(response),
             0);

        printf("Authentication failed.\n");
    }

    close(client_fd);
    close(server_fd);

    printf("Agent stopped.\n");

    return 0;
}
