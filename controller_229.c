#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_PORT 9410
#define BUFFER_SIZE 1024

int main(void)
{
    int sock_fd;

    struct sockaddr_in server_addr;

    char buffer[BUFFER_SIZE];

    /* 1. Create the TCP socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0)
    {
        perror("socket");
        return 1;
    }

    printf("TCP socket created successfully.\n");

    /* 2. Prepare the Agent address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, "127.0.0.1",
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sock_fd);
        return 1;
    }

    /* 3. Connect to the Agent */
    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock_fd);
        return 1;
    }

    printf("Connected to RemoteOps Agent.\n");

    /* 4. Receive the Agent's test message */
    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received = recv(sock_fd,
                                  buffer,
                                  sizeof(buffer) - 1,
                                  0);

    if (bytes_received < 0)
    {
        perror("recv");
        close(sock_fd);
        return 1;
    }

    buffer[bytes_received] = '\0';

    printf("Agent says: %s", buffer);

    /* 5. Close the connection */
    close(sock_fd);

    printf("Controller stopped.\n");

    return 0;
}
