#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_PORT 9410
#define UDP_PORT 9500
#define BUFFER_SIZE 8192

#define AUTH_TOKEN "OPS-2229"
#define SESSION_ID "9222"

int send_all(int sock, const char *data, size_t length)
{
    size_t total = 0;

    while (total < length)
    {
        ssize_t sent = send(sock, data + total, length - total, 0);

        if (sent <= 0)
        {
            return -1;
        }

        total += sent;
    }

    return 0;
}

int receive_line(int sock, char *buffer, size_t buffer_size)
{
    size_t i = 0;

    while (i < buffer_size - 1)
    {
        char c;

        ssize_t received = recv(sock, &c, 1, 0);

        if (received <= 0)
        {
            return -1;
        }

        buffer[i++] = c;

        if (c == '\n')
        {
            break;
        }
    }

    buffer[i] = '\0';

    return 0;
}

int main()
{
    int tcp_socket;
    int udp_socket;

    struct sockaddr_in server_address;
    struct sockaddr_in udp_address;

    char buffer[BUFFER_SIZE];

    /* Create TCP socket */
    tcp_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (tcp_socket < 0)
    {
        perror("TCP socket");
        return 1;
    }

    memset(&server_address, 0, sizeof(server_address));

    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    /* Connect to Agent */
    if (connect(tcp_socket,
                (struct sockaddr *)&server_address,
                sizeof(server_address)) < 0)
    {
        perror("connect");
        close(tcp_socket);
        return 1;
    }

    printf("Connected to Agent on TCP port %d.\n", SERVER_PORT);

    /*  AUTH  */

    snprintf(buffer,
             sizeof(buffer),
             "AUTH %s\n",
             AUTH_TOKEN);

    send_all(tcp_socket, buffer, strlen(buffer));

    if (receive_line(tcp_socket, buffer, sizeof(buffer)) < 0)
    {
        printf("Failed to receive authentication response.\n");
        close(tcp_socket);
        return 1;
    }

    printf("Agent: %s", buffer);

    if (strncmp(buffer, "OK AUTHENTICATED", 15) != 0)
    {
        printf("Authentication failed.\n");
        close(tcp_socket);
        return 1;
    }

    printf("Authentication successful.\n\n");

    /*  UDP SOCKET  */

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (udp_socket < 0)
    {
        perror("UDP socket");
        close(tcp_socket);
        return 1;
    }

    memset(&udp_address, 0, sizeof(udp_address));

    udp_address.sin_family = AF_INET;
    udp_address.sin_port = htons(UDP_PORT);
    udp_address.sin_addr.s_addr = htonl(INADDR_ANY);

    /*
     * Bind UDP socket before starting monitoring.
     * This prevents the first monitoring packet from being lost.
     */
    if (bind(udp_socket,
             (struct sockaddr *)&udp_address,
             sizeof(udp_address)) < 0)
    {
        perror("UDP bind");
        close(udp_socket);
        close(tcp_socket);
        return 1;
    }

    printf("UDP listener ready on port %d.\n\n", UDP_PORT);

    /*  MONITOR START  */

    snprintf(buffer,
             sizeof(buffer),
             "MONITOR START %d\n",
             UDP_PORT);

    send_all(tcp_socket, buffer, strlen(buffer));

    if (receive_line(tcp_socket, buffer, sizeof(buffer)) < 0)
    {
        printf("Failed to receive MONITOR START response.\n");
        close(udp_socket);
        close(tcp_socket);
        return 1;
    }

    printf("Agent: %s", buffer);

    if (strncmp(buffer, "OK MONITOR_STARTED", 18) != 0)
    {
        printf("Monitoring failed to start.\n");
        close(udp_socket);
        close(tcp_socket);
        return 1;
    }

    printf("UDP monitoring started.\n\n");

    /*  RECEIVE MONITOR DATA  */

    for (int i = 1; i <= 5; i++)
    {
        char monitor_buffer[BUFFER_SIZE];

        ssize_t received = recvfrom(
            udp_socket,
            monitor_buffer,
            sizeof(monitor_buffer) - 1,
            0,
            NULL,
            NULL
        );

        if (received < 0)
        {
            perror("recvfrom");
            break;
        }

        monitor_buffer[received] = '\0';

        printf("Monitoring message %d:\n", i);
        printf("%s\n", monitor_buffer);
    }

    /*  MONITOR STOP  */

    printf("Stopping UDP monitoring...\n");

    snprintf(buffer,
             sizeof(buffer),
             "MONITOR STOP\n");

    send_all(tcp_socket, buffer, strlen(buffer));

    if (receive_line(tcp_socket, buffer, sizeof(buffer)) < 0)
    {
        printf("Failed to receive MONITOR STOP response.\n");
    }
    else
    {
        printf("Agent: %s", buffer);
    }

    printf("UDP monitoring stopped.\n");

    /*  CLEANUP  */

    close(udp_socket);
    close(tcp_socket);

    printf("Controller closed.\n");

    return 0;
}
