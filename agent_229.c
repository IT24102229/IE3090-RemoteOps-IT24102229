#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9410
#define BUFFER_SIZE 1024

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    char buffer[BUFFER_SIZE];

    /* 1. Create the TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    printf("TCP socket created successfully.\n");

    /* 2. Prepare the server address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    /* 3. Bind the socket to port 9410 */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    printf("Agent bound to port %d.\n", PORT);

    /* 4. Start listening */
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Agent is listening...\n");

    /* 5. Wait for a Controller */
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

    /* 6. Send a simple test message */
    strcpy(buffer, "Hello from RemoteOps Agent!\n");

    send(client_fd, buffer, strlen(buffer), 0);

    printf("Test message sent to Controller.\n");

    /* 7. Close the connection */
    close(client_fd);
    close(server_fd);

    printf("Agent stopped.\n");

    return 0;
}
