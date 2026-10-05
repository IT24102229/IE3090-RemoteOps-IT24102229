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

    /* Create TCP socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0)
    {
        perror("socket");
        return 1;
    }

    printf("TCP socket created successfully.\n");

    /* Prepare Agent address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET,
                  "127.0.0.1",
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sock_fd);
        return 1;
    }

    /* Connect */
    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock_fd);
        return 1;
    }

    printf("Connected to RemoteOps Agent.\n");

    /*
     * Send AUTH
     */
    const char *auth_command =
        "AUTH OPS-2229\n";

    send(sock_fd,
         auth_command,
         strlen(auth_command),
         0);

    printf("Sent: %s", auth_command);

    /* Receive AUTH response */
    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received =
        recv(sock_fd,
             buffer,
             sizeof(buffer) - 1,
             0);

    if (bytes_received <= 0)
    {
        printf("Agent disconnected.\n");
        close(sock_fd);
        return 1;
    }

    buffer[bytes_received] = '\0';

    printf("Agent response: %s", buffer);
    /*
 * Send LISTPROC
 */
const char *listproc_command =
    "LISTPROC\n";

send(sock_fd,
     listproc_command,
     strlen(listproc_command),
     0);

printf("Sent: %s", listproc_command);

/*
 * Receive LISTPROC response
 */
memset(buffer, 0, sizeof(buffer));

bytes_received =
    recv(sock_fd,
         buffer,
         sizeof(buffer) - 1,
         0);

if (bytes_received <= 0)
{
    printf("Agent disconnected.\n");
    close(sock_fd);
    return 1;
}

buffer[bytes_received] = '\0';

printf("Agent response: %s", buffer);
    /*
     * Send SYSINFO
     */
    const char *sysinfo_command =
        "SYSINFO\n";

    send(sock_fd,
         sysinfo_command,
         strlen(sysinfo_command),
         0);

    printf("Sent: %s", sysinfo_command);

    /* Receive SYSINFO response */
    memset(buffer, 0, sizeof(buffer));

    bytes_received =
        recv(sock_fd,
             buffer,
             sizeof(buffer) - 1,
             0);

    if (bytes_received <= 0)
    {
        printf("Agent disconnected.\n");
        close(sock_fd);
        return 1;
    }

    buffer[bytes_received] = '\0';

    printf("Agent response: %s", buffer);

    close(sock_fd);

    printf("Controller stopped.\n");

    return 0;
}
