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

void get_sysinfo(char *response, size_t response_size)
{
    FILE *fp;

    double uptime;
    double load_average;

    long mem_total_kb = 0;
    long mem_available_kb = 0;

    /*
     * Get uptime and load average
     */
    fp = fopen("/proc/uptime", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &uptime);
        fclose(fp);
    }
    else
    {
        uptime = 0;
    }

    /*
     * Get CPU load from /proc/loadavg
     */
    fp = fopen("/proc/loadavg", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &load_average);
        fclose(fp);
    }
    else
    {
        load_average = 0;
    }

    /*
     * Get memory information
     */
    fp = fopen("/proc/meminfo", "r");

    if (fp != NULL)
    {
        char line[256];

        while (fgets(line, sizeof(line), fp))
        {
            if (sscanf(line, "MemTotal: %ld kB",
                       &mem_total_kb) == 1)
            {
                continue;
            }

            if (sscanf(line, "MemAvailable: %ld kB",
                       &mem_available_kb) == 1)
            {
                continue;
            }
        }

        fclose(fp);
    }

    long mem_used_mb =
        (mem_total_kb - mem_available_kb) / 1024;

    snprintf(response,
             response_size,
             "OK SYSINFO %.2f %ld %.0f SID:%s\n",
             load_average,
             mem_used_mb,
             uptime,
             SESSION_ID);
}

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    char buffer[BUFFER_SIZE];

    int authenticated = 0;

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

    /* Bind */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    printf("Agent bound to port %d.\n", PORT);

    /* Listen */
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Agent is listening...\n");

    /* Accept one Controller */
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
     * Command processing loop
     */
    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        ssize_t bytes_received =
            recv(client_fd,
                 buffer,
                 sizeof(buffer) - 1,
                 0);

        if (bytes_received <= 0)
        {
            printf("Controller disconnected.\n");
            break;
        }

        buffer[bytes_received] = '\0';

        printf("Received: %s", buffer);

        /*
         * AUTH command
         */
        if (strncmp(buffer, "AUTH ", 5) == 0)
        {
            char expected_command[BUFFER_SIZE];

            snprintf(expected_command,
                     sizeof(expected_command),
                     "AUTH %s\n",
                     AUTH_TOKEN);

            if (strcmp(buffer, expected_command) == 0)
            {
                char response[BUFFER_SIZE];

                authenticated = 1;

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

            continue;
        }

        /*
         * Reject commands before authentication
         */
        if (!authenticated)
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

            continue;
        }

        /*
         * SYSINFO command
         */
        if (strcmp(buffer, "SYSINFO\n") == 0)
        {
            char response[BUFFER_SIZE];

            get_sysinfo(response,
                        sizeof(response));

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            printf("SYSINFO sent.\n");
        }
        else
        {
            char response[BUFFER_SIZE];

            snprintf(response,
                     sizeof(response),
                     "ERR 002 COMMAND_NOT_ALLOWED SID:%s\n",
                     SESSION_ID);

            send(client_fd,
                 response,
                 strlen(response),
                 0);
        }
    }

    close(client_fd);
    close(server_fd);

    printf("Agent stopped.\n");

    return 0;
}
