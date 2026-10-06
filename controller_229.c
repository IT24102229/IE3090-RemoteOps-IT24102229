#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>


#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9410

#define UDP_PORT 9500

#define BUFFER_SIZE 8192


/* -------------------------------------------------- */
/* Send all bytes                                     */
/* -------------------------------------------------- */

int send_all(int sock_fd,
             const char *data,
             size_t length)
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


/* -------------------------------------------------- */
/* Receive one line                                   */
/* -------------------------------------------------- */

int recv_line(int sock_fd,
              char *buffer,
              size_t size)
{
    size_t position = 0;

    while (position < size - 1)
    {
        char ch;

        ssize_t received =
            recv(sock_fd,
                 &ch,
                 1,
                 0);

        if (received == 0)
        {
            return 0;
        }

        if (received < 0)
        {
            return -1;
        }

        buffer[position++] = ch;

        if (ch == '\n')
        {
            break;
        }
    }

    buffer[position] = '\0';

    return (int)position;
}


/* -------------------------------------------------- */
/* Send a file using PUT                              */
/* -------------------------------------------------- */

int upload_file(int sock_fd,
                const char *local_filename,
                const char *remote_filename)
{
    FILE *file =
        fopen(local_filename, "rb");

    if (file == NULL)
    {
        perror("Cannot open local file");
        return -1;
    }


    /* Get file size */

    fseek(file, 0, SEEK_END);

    long filesize =
        ftell(file);

    fseek(file, 0, SEEK_SET);


    /* Send PUT header */

    char header[BUFFER_SIZE];

    snprintf(header,
             sizeof(header),
             "PUT %s %ld\n",
             remote_filename,
             filesize);


    if (send_all(sock_fd,
                 header,
                 strlen(header)) < 0)
    {
        fclose(file);
        return -1;
    }


    /* Send file bytes */

    char buffer[BUFFER_SIZE];

    size_t bytes_read;


    while ((bytes_read =
            fread(buffer,
                  1,
                  sizeof(buffer),
                  file)) > 0)
    {
        if (send_all(sock_fd,
                     buffer,
                     bytes_read) < 0)
        {
            fclose(file);
            return -1;
        }
    }


    fclose(file);

    return 0;
}


/* -------------------------------------------------- */
/* Receive exact file bytes                           */
/* -------------------------------------------------- */

int receive_file_data(int sock_fd,
                      FILE *file,
                      long filesize)
{
    char buffer[BUFFER_SIZE];

    long remaining = filesize;


    while (remaining > 0)
    {
        size_t amount =
            sizeof(buffer);


        if (remaining < (long)amount)
        {
            amount =
                (size_t)remaining;
        }


        ssize_t received =
            recv(sock_fd,
                 buffer,
                 amount,
                 0);


        if (received <= 0)
        {
            return -1;
        }


        size_t written =
            fwrite(buffer,
                   1,
                   received,
                   file);


        if (written !=
            (size_t)received)
        {
            return -1;
        }


        remaining -= received;
    }


    return 0;
}


/* -------------------------------------------------- */
/* Download file using GET                            */
/* -------------------------------------------------- */

int download_file(int sock_fd,
                  const char *remote_filename,
                  const char *local_filename)
{
    char command[BUFFER_SIZE];

    char response[BUFFER_SIZE];


    /* Send GET command */

    snprintf(command,
             sizeof(command),
             "GET %s\n",
             remote_filename);


    if (send_all(sock_fd,
                 command,
                 strlen(command)) < 0)
    {
        return -1;
    }


    /* Receive response */

    if (recv_line(sock_fd,
                  response,
                  sizeof(response)) <= 0)
    {
        return -1;
    }


    printf("Agent: %s",
           response);


    /* Check whether file exists */

    if (strncmp(response,
                "OK FILE_SEND",
                12) != 0)
    {
        return 0;
    }


    char received_filename[256];

    long filesize;


    if (sscanf(response,
               "OK FILE_SEND %255s %ld",
               received_filename,
               &filesize) != 2)
    {
        printf("Invalid GET response.\n");

        return -1;
    }


    /* Create local file */

    FILE *file =
        fopen(local_filename, "wb");


    if (file == NULL)
    {
        perror("Cannot create local file");

        return -1;
    }


    /* Receive exact bytes */

    if (receive_file_data(sock_fd,
                          file,
                          filesize) < 0)
    {
        fclose(file);

        return -1;
    }


    fclose(file);


    printf("File downloaded successfully.\n");
    printf("Saved as: %s\n", local_filename);
    printf("Size: %ld bytes\n",
           filesize);


    return 0;
}


/* -------------------------------------------------- */
/* Create UDP listener                                */
/* -------------------------------------------------- */

int create_udp_socket(int port)
{
    int udp_socket;


    udp_socket =
        socket(AF_INET,
               SOCK_DGRAM,
               0);


    if (udp_socket < 0)
    {
        perror("UDP socket");

        return -1;
    }


    struct sockaddr_in udp_address;


    memset(&udp_address,
           0,
           sizeof(udp_address));


    udp_address.sin_family =
        AF_INET;


    udp_address.sin_port =
        htons(port);


    udp_address.sin_addr.s_addr =
        htonl(INADDR_ANY);


    if (bind(udp_socket,
             (struct sockaddr *)&udp_address,
             sizeof(udp_address)) < 0)
    {
        perror("UDP bind");

        close(udp_socket);

        return -1;
    }


    return udp_socket;
}


/* -------------------------------------------------- */
/* Display any waiting UDP monitoring messages        */
/* -------------------------------------------------- */

void check_udp_messages(int udp_socket)
{
    if (udp_socket < 0)
    {
        return;
    }


    while (1)
    {
        fd_set readfds;

        struct timeval timeout;


        FD_ZERO(&readfds);

        FD_SET(udp_socket,
               &readfds);


        timeout.tv_sec = 0;
        timeout.tv_usec = 0;


        int result =
            select(udp_socket + 1,
                   &readfds,
                   NULL,
                   NULL,
                   &timeout);


        if (result <= 0)
        {
            break;
        }


        if (FD_ISSET(udp_socket,
                     &readfds))
        {
            char message[BUFFER_SIZE];


            ssize_t received =
                recvfrom(udp_socket,
                         message,
                         sizeof(message) - 1,
                         0,
                         NULL,
                         NULL);


            if (received <= 0)
            {
                break;
            }


            message[received] =
                '\0';


            printf("\n[UDP Monitoring]\n");
            printf("%s",
                   message);
        }
        else
        {
            break;
        }
    }
}


/* -------------------------------------------------- */
/* Main                                               */
/* -------------------------------------------------- */

int main(void)
{
    int sock_fd;

    int udp_socket = -1;

    int monitoring = 0;


    struct sockaddr_in server_addr;


    /* ------------------------------------------------ */
    /* Create TCP socket                                */
    /* ------------------------------------------------ */

    sock_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);


    if (sock_fd < 0)
    {
        perror("socket");

        return 1;
    }


    /* ------------------------------------------------ */
    /* Prepare Agent address                            */
    /* ------------------------------------------------ */

    memset(&server_addr,
           0,
           sizeof(server_addr));


    server_addr.sin_family =
        AF_INET;


    server_addr.sin_port =
        htons(SERVER_PORT);


    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");

        close(sock_fd);

        return 1;
    }


    /* ------------------------------------------------ */
    /* Connect to Agent                                 */
    /* ------------------------------------------------ */

    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");

        close(sock_fd);

        return 1;
    }


    printf("Connected to RemoteOps Agent.\n");
    printf("Type commands at the RemoteOps> prompt.\n");
    printf("Example: AUTH OPS-2229\n\n");


    char command[BUFFER_SIZE];

    char response[BUFFER_SIZE];


    /* ------------------------------------------------ */
    /* Interactive command loop                         */
    /* ------------------------------------------------ */

    while (1)
    {
        /*
         * If UDP monitoring is running,
         * display any monitoring messages
         * that have arrived.
         */

        if (monitoring)
        {
            check_udp_messages(udp_socket);
        }


        printf("RemoteOps> ");

        fflush(stdout);


        if (fgets(command,
                  sizeof(command),
                  stdin) == NULL)
        {
            break;
        }


        /* Remove newline */

        command[strcspn(command,
                        "\n")] = '\0';


        if (strlen(command) == 0)
        {
            continue;
        }


        /* ------------------------------------------------ */
        /* QUIT                                              */
        /* ------------------------------------------------ */

        if (strcmp(command,
                   "QUIT") == 0)
        {
            char quit_command[] =
                "QUIT\n";


            if (send_all(sock_fd,
                         quit_command,
                         strlen(quit_command)) < 0)
            {
                break;
            }


            if (recv_line(sock_fd,
                          response,
                          sizeof(response)) > 0)
            {
                printf("Agent: %s",
                       response);
            }


            break;
        }


        /* ------------------------------------------------ */
        /* PUT                                               */
        /* ------------------------------------------------ */

        else if (strcmp(command,
                        "PUT") == 0)
        {
            char local_filename[256];

            char remote_filename[256];


            printf("Enter local filename: ");

            fflush(stdout);


            if (fgets(local_filename,
                      sizeof(local_filename),
                      stdin) == NULL)
            {
                break;
            }


            local_filename[strcspn(
                local_filename,
                "\n")] = '\0';


            printf("Enter remote filename: ");

            fflush(stdout);


            if (fgets(remote_filename,
                      sizeof(remote_filename),
                      stdin) == NULL)
            {
                break;
            }


            remote_filename[strcspn(
                remote_filename,
                "\n")] = '\0';


            if (upload_file(sock_fd,
                            local_filename,
                            remote_filename) < 0)
            {
                printf("PUT failed.\n");

                continue;
            }


            if (recv_line(sock_fd,
                          response,
                          sizeof(response)) > 0)
            {
                printf("Agent: %s",
                       response);
            }
        }


        /* ------------------------------------------------ */
        /* GET                                               */
        /* ------------------------------------------------ */

        else if (strcmp(command,
                        "GET") == 0)
        {
            char remote_filename[256];

            char local_filename[256];


            printf("Enter remote filename: ");

            fflush(stdout);


            if (fgets(remote_filename,
                      sizeof(remote_filename),
                      stdin) == NULL)
            {
                break;
            }


            remote_filename[strcspn(
                remote_filename,
                "\n")] = '\0';


            printf("Enter local filename: ");

            fflush(stdout);


            if (fgets(local_filename,
                      sizeof(local_filename),
                      stdin) == NULL)
            {
                break;
            }


            local_filename[strcspn(
                local_filename,
                "\n")] = '\0';


            download_file(sock_fd,
                          remote_filename,
                          local_filename);
        }


        /* ------------------------------------------------ */
        /* MONITOR START                                    */
        /* ------------------------------------------------ */

        else if (strcmp(command,
                        "MONITOR START") == 0)
        {
            if (monitoring)
            {
                printf("Monitoring is already running.\n");

                continue;
            }


            int port;


            printf("Enter UDP port [%d]: ",
                   UDP_PORT);

            fflush(stdout);


            char port_input[32];


            if (fgets(port_input,
                      sizeof(port_input),
                      stdin) == NULL)
            {
                break;
            }


            if (strlen(port_input) <= 1)
            {
                port = UDP_PORT;
            }
            else
            {
                port =
                    atoi(port_input);
            }


            if (port <= 0 ||
                port > 65535)
            {
                printf("Invalid UDP port.\n");

                continue;
            }


            /*
             * Bind the UDP socket BEFORE
             * telling the Agent to start.
             */

            udp_socket =
                create_udp_socket(port);


            if (udp_socket < 0)
            {
                continue;
            }


            char monitor_command[BUFFER_SIZE];


            snprintf(monitor_command,
                     sizeof(monitor_command),
                     "MONITOR START %d\n",
                     port);


            if (send_all(sock_fd,
                         monitor_command,
                         strlen(monitor_command)) < 0)
            {
                close(udp_socket);

                udp_socket = -1;

                continue;
            }


            if (recv_line(sock_fd,
                          response,
                          sizeof(response)) <= 0)
            {
                close(udp_socket);

                udp_socket = -1;

                continue;
            }


            printf("Agent: %s",
                   response);


            if (strncmp(response,
                        "OK MONITOR_STARTED",
                        18) == 0)
            {
                monitoring = 1;

                printf("UDP monitoring started.\n");
                printf("Monitoring messages will appear above the prompt.\n");
            }
            else
            {
                close(udp_socket);

                udp_socket = -1;
            }
        }


        /* ------------------------------------------------ */
        /* MONITOR STOP                                     */
        /* ------------------------------------------------ */

        else if (strcmp(command,
                        "MONITOR STOP") == 0)
        {
            if (!monitoring)
            {
                printf("Monitoring is not running.\n");

                continue;
            }


            char stop_command[] =
                "MONITOR STOP\n";


            if (send_all(sock_fd,
                         stop_command,
                         strlen(stop_command)) < 0)
            {
                break;
            }


            if (recv_line(sock_fd,
                          response,
                          sizeof(response)) > 0)
            {
                printf("Agent: %s",
                       response);
            }


            monitoring = 0;


            close(udp_socket);

            udp_socket = -1;


            printf("UDP monitoring stopped.\n");
        }


        /* ------------------------------------------------ */
        /* Normal TCP commands                              */
        /* ------------------------------------------------ */

        else
        {
            /*
             * Add the newline required by the
             * RemoteOps line-based protocol.
             */

            char command_line[BUFFER_SIZE];


            snprintf(command_line,
                     sizeof(command_line),
                     "%s\n",
                     command);


            if (send_all(sock_fd,
                         command_line,
                         strlen(command_line)) < 0)
            {
                perror("send");

                break;
            }


            int result =
                recv_line(sock_fd,
                          response,
                          sizeof(response));


            if (result == 0)
            {
                printf("Agent disconnected.\n");

                break;
            }


            if (result < 0)
            {
                perror("recv");

                break;
            }


            printf("Agent: %s",
                   response);
        }
    }


    /* ------------------------------------------------ */
    /* Cleanup                                          */
    /* ------------------------------------------------ */

    if (udp_socket >= 0)
    {
        close(udp_socket);
    }


    close(sock_fd);


    printf("\nController closed.\n");


    return 0;
}
