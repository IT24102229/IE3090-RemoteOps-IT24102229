#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 9410
#define BUFFER_SIZE 8192


int send_all(int sock_fd, const char *data, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent =
            send(sock_fd,
                 data + total_sent,
                 length - total_sent,
                 0);

        if (sent <= 0)
        {
            return -1;
        }

        total_sent += sent;
    }

    return 0;
}


int receive_line(int sock_fd,
                 char *buffer,
                 size_t buffer_size)
{
    size_t position = 0;

    while (position < buffer_size - 1)
    {
        char character;

        ssize_t received =
            recv(sock_fd,
                 &character,
                 1,
                 0);

        if (received <= 0)
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


int send_file(int sock_fd,
              const char *filename,
              long filesize)
{
    FILE *file;

    char buffer[BUFFER_SIZE];

    long remaining = filesize;

    file = fopen(filename, "rb");

    if (file == NULL)
    {
        perror("Could not open test file");
        return -1;
    }

    while (remaining > 0)
    {
        size_t amount = sizeof(buffer);

        if (remaining < (long)amount)
        {
            amount = (size_t)remaining;
        }

        size_t bytes_read =
            fread(buffer,
                  1,
                  amount,
                  file);

        if (bytes_read == 0)
        {
            fclose(file);
            return -1;
        }

        if (send_all(sock_fd,
                     buffer,
                     bytes_read) < 0)
        {
            fclose(file);
            return -1;
        }

        remaining -= bytes_read;
    }

    fclose(file);

    return 0;
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

    send_all(sock_fd,
             auth_command,
             strlen(auth_command));

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


    /* PUT */

    const char *local_filename =
        "test_upload.txt";

    const char *remote_filename =
        "test_upload.txt";

    FILE *test_file =
        fopen(local_filename, "rb");

    if (test_file == NULL)
    {
        perror("Could not open test file");
        close(sock_fd);
        return 1;
    }

    fseek(test_file, 0, SEEK_END);

    long filesize =
        ftell(test_file);

    fclose(test_file);

    printf("Uploading %s (%ld bytes)\n",
           local_filename,
           filesize);

    char put_command[BUFFER_SIZE];

    snprintf(put_command,
             sizeof(put_command),
             "PUT %s %ld\n",
             remote_filename,
             filesize);

    send_all(sock_fd,
             put_command,
             strlen(put_command));

    printf("Sent: %s", put_command);

    if (send_file(sock_fd,
                  local_filename,
                  filesize) < 0)
    {
        printf("File upload failed.\n");
        close(sock_fd);
        return 1;
    }

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


    close(sock_fd);

    printf("Controller stopped.\n");

    return 0;
}
