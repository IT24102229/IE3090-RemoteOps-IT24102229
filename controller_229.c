#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9410
#define BUFFER_SIZE 8192


int receive_line(int sock_fd, char *buffer, size_t buffer_size)
{
    size_t position = 0;

    while (position < buffer_size - 1)
    {
        char character;

        int bytes_received =
            recv(sock_fd, &character, 1, 0);

        if (bytes_received <= 0)
        {
            return -1;
        }

        buffer[position++] = character;

        if (character == '\n')
        {
            break;
        }
    }

    buffer[position] = '\0';

    return (int)position;
}


int main()
{
    int sock_fd;

    struct sockaddr_in server_addr;

    char buffer[BUFFER_SIZE];

    sock_fd = socket(AF_INET,
                     SOCK_STREAM,
                     0);

    if (sock_fd < 0)
    {
        perror("Socket creation failed");
        return 1;
    }

    printf("TCP socket created successfully.\n");

    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    server_addr.sin_addr.s_addr =
        htonl(INADDR_LOOPBACK);

    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("Connection failed");
        close(sock_fd);
        return 1;
    }

    printf("Connected to RemoteOps Agent.\n");


    /* Authentication */

    const char *auth_command =
        "AUTH OPS-2229\n";

    send(sock_fd,
         auth_command,
         strlen(auth_command),
         0);

    printf("Sent: %s", auth_command);

    memset(buffer, 0, sizeof(buffer));

    if (receive_line(sock_fd,
                     buffer,
                     sizeof(buffer)) < 0)
    {
        printf("Connection closed by Agent.\n");
        close(sock_fd);
        return 1;
    }

    printf("Agent response: %s", buffer);


    /* LISTPROC */

    const char *listproc_command =
        "LISTPROC\n";

    send(sock_fd,
         listproc_command,
         strlen(listproc_command),
         0);

    printf("Sent: %s", listproc_command);

    memset(buffer, 0, sizeof(buffer));

    if (receive_line(sock_fd,
                     buffer,
                     sizeof(buffer)) < 0)
    {
        printf("Connection closed by Agent.\n");
        close(sock_fd);
        return 1;
    }

    printf("Agent response: %s", buffer);


    /* EXEC commands */

    const char *exec_commands[] =
    {
        "EXEC DATE\n",
        "EXEC UPTIME\n",
        "EXEC DISKFREE\n",
        "EXEC HOSTNAME\n",
        "EXEC WHOAMI\n"
    };

    int number_of_commands = 5;

    for (int i = 0;
         i < number_of_commands;
         i++)
    {
        send(sock_fd,
             exec_commands[i],
             strlen(exec_commands[i]),
             0);

        printf("Sent: %s",
               exec_commands[i]);

        memset(buffer, 0, sizeof(buffer));

        if (receive_line(sock_fd,
                         buffer,
                         sizeof(buffer)) < 0)
        {
            printf("Connection closed by Agent.\n");
            break;
        }

        printf("Agent response: %s",
               buffer);
    }


    /* SYSINFO */

    const char *sysinfo_command =
        "SYSINFO\n";

    send(sock_fd,
         sysinfo_command,
         strlen(sysinfo_command),
         0);

    printf("Sent: %s", sysinfo_command);

    memset(buffer, 0, sizeof(buffer));

    if (receive_line(sock_fd,
                     buffer,
                     sizeof(buffer)) >= 0)
    {
        printf("Agent response: %s",
               buffer);
    }


    close(sock_fd);

    printf("Controller stopped.\n");

    return 0;
}
