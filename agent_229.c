#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#define PORT 9410
#define BUFFER_SIZE 8192

#define AUTH_TOKEN "OPS-2229"
#define SESSION_ID "9222"


/*
 * SYSINFO
 */
void get_sysinfo(char *response, size_t response_size)
{
    FILE *fp;

    double uptime = 0;
    double load_average = 0;

    long mem_total_kb = 0;
    long mem_available_kb = 0;

    /*
     * Get uptime.
     */
    fp = fopen("/proc/uptime", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &uptime);
        fclose(fp);
    }

    /*
     * Get CPU load.
     */
    fp = fopen("/proc/loadavg", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &load_average);
        fclose(fp);
    }

    /*
     * Get memory information.
     */
    fp = fopen("/proc/meminfo", "r");

    if (fp != NULL)
    {
        char line[256];

        while (fgets(line, sizeof(line), fp) != NULL)
        {
            if (sscanf(line,
                       "MemTotal: %ld kB",
                       &mem_total_kb) == 1)
            {
                continue;
            }

            if (sscanf(line,
                       "MemAvailable: %ld kB",
                       &mem_available_kb) == 1)
            {
                continue;
            }
        }

        fclose(fp);
    }

    /*
     * Calculate used memory in MB.
     */
    long mem_used_mb =
        (mem_total_kb - mem_available_kb) / 1024;

    /*
     * Create SYSINFO response.
     */
    snprintf(response,
             response_size,
             "OK SYSINFO %.2f %ld %.0f SID:%s\n",
             load_average,
             mem_used_mb,
             uptime,
             SESSION_ID);
}


/*
 * LISTPROC
 */
void get_process_list(char *response, size_t response_size)
{
    FILE *fp;
    char line[256];

    size_t used = 0;

    /*
     * Run ps to get PID and process name.
     */
    fp = popen("ps -e -o pid=,comm=", "r");

    if (fp == NULL)
    {
        snprintf(response,
                 response_size,
                 "ERR 003 PROCESS_LIST_FAILED SID:%s\n",
                 SESSION_ID);

        return;
    }

    /*
     * Start the response.
     */
    used = snprintf(response,
                    response_size,
                    "OK PROCS ");

    /*
     * Read each process.
     */
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char pid[32];
        char name[128];

        /*
         * Remove newline.
         */
        line[strcspn(line, "\n")] = '\0';

        /*
         * Extract PID and process name.
         */
        if (sscanf(line,
                   "%31s %127s",
                   pid,
                   name) == 2)
        {
            int written;

            /*
             * Add comma between processes.
             */
            if (used > strlen("OK PROCS "))
            {
                written = snprintf(response + used,
                                   response_size - used,
                                   ",");

                if (written < 0 ||
                    (size_t)written >= response_size - used)
                {
                    break;
                }

                used += written;
            }

            /*
             * Add PID/process-name pair.
             */
            written = snprintf(response + used,
                               response_size - used,
                               "%s/%s",
                               pid,
                               name);

            if (written < 0 ||
                (size_t)written >= response_size - used)
            {
                break;
            }

            used += written;
        }
    }

    /*
     * Close ps.
     */
    pclose(fp);

    /*
     * Add SID.
     */
    if (used < response_size)
    {
        snprintf(response + used,
                 response_size - used,
                 " SID:%s\n",
                 SESSION_ID);
    }
}


/*
 * EXEC
 *
 * Only these commands are allowed:
 *
 * DATE
 * UPTIME
 * DISKFREE
 * HOSTNAME
 * WHOAMI
 *
 * Arbitrary commands are NOT accepted.
 */
void execute_command(char *command,
                     char *response,
                     size_t response_size)
{
    const char *system_command = NULL;

    FILE *fp;

    char output[512];

    /*
     * Check the fixed whitelist.
     */
    if (strcmp(command, "DATE") == 0)
    {
        system_command = "date";
    }
    else if (strcmp(command, "UPTIME") == 0)
    {
        system_command = "uptime";
    }
    else if (strcmp(command, "DISKFREE") == 0)
    {
        system_command = "df -h .";
    }
    else if (strcmp(command, "HOSTNAME") == 0)
    {
        system_command = "hostname";
    }
    else if (strcmp(command, "WHOAMI") == 0)
    {
        system_command = "whoami";
    }
    else
    {
        /*
         * Command is not in the whitelist.
         */
        snprintf(response,
                 response_size,
                 "ERR 004 UNKNOWN_EXEC SID:%s\n",
                 SESSION_ID);

        return;
    }

    /*
     * Execute the predefined command.
     */
    fp = popen(system_command, "r");

    if (fp == NULL)
    {
        snprintf(response,
                 response_size,
                 "ERR 005 EXEC_FAILED SID:%s\n",
                 SESSION_ID);

        return;
    }

    /*
     * Clear output buffer.
     */
    memset(output, 0, sizeof(output));

    /*
     * Read command output.
     */
    if (fgets(output,
              sizeof(output),
              fp) == NULL)
    {
        pclose(fp);

        snprintf(response,
                 response_size,
                 "OK EXEC %s 0 SID:%s\n",
                 command,
                 SESSION_ID);

        return;
    }

    /*
     * Remove newline from output.
     */
    output[strcspn(output, "\n")] = '\0';

    /*
     * Get exit status.
     */
    int status = pclose(fp);

    int exit_code = 0;

    if (WIFEXITED(status))
    {
        exit_code = WEXITSTATUS(status);
    }

    /*
     * Create EXEC response.
     */
    snprintf(response,
             response_size,
             "OK EXEC %s %d %s SID:%s\n",
             command,
             exit_code,
             output,
             SESSION_ID);
}


/*
 * MAIN
 */
int main()
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    char buffer[BUFFER_SIZE];

    /*
     * Create TCP socket.
     */
    server_fd = socket(AF_INET,
                       SOCK_STREAM,
                       0);

    if (server_fd < 0)
    {
        perror("Socket creation failed");
        return 1;
    }

    printf("TCP socket created successfully.\n");

    /*
     * Configure server address.
     */
    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    /*
     * Bind socket to port.
     */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("Bind failed");

        close(server_fd);

        return 1;
    }

    printf("Agent bound to port %d.\n",
           PORT);

    /*
     * Start listening.
     */
    if (listen(server_fd, 5) < 0)
    {
        perror("Listen failed");

        close(server_fd);

        return 1;
    }

    printf("Agent is listening...\n");

    /*
     * Accept Controller connection.
     */
    client_fd = accept(server_fd,
                       (struct sockaddr *)&client_addr,
                       &client_len);

    if (client_fd < 0)
    {
        perror("Accept failed");

        close(server_fd);

        return 1;
    }

    printf("Controller connected successfully!\n");

    /*
     * Authentication status.
     */
    int authenticated = 0;

    /*
     * Receive commands.
     */
    while (1)
    {
        memset(buffer,
               0,
               sizeof(buffer));

        int bytes_received =
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

        printf("Received: %s",
               buffer);


        /*
         * AUTH
         */
        if (strncmp(buffer,
                    "AUTH ",
                    5) == 0)
        {
            char expected_command[BUFFER_SIZE];

            snprintf(expected_command,
                     sizeof(expected_command),
                     "AUTH %s\n",
                     AUTH_TOKEN);

            /*
             * Correct authentication token.
             */
            if (strcmp(buffer,
                       expected_command) == 0)
            {
                authenticated = 1;

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

            /*
             * Incorrect authentication token.
             */
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
        }


        /*
         * REJECT COMMANDS BEFORE AUTHENTICATION
         */
        else if (!authenticated)
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
        }


        /*
         * SYSINFO
         */
        else if (strcmp(buffer,
                        "SYSINFO\n") == 0)
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


        /*
         * LISTPROC
         */
        else if (strcmp(buffer,
                        "LISTPROC\n") == 0)
        {
            char response[BUFFER_SIZE];

            get_process_list(response,
                             sizeof(response));

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            printf("LISTPROC sent.\n");
        }


        /*
         * EXEC
         */
        else if (strncmp(buffer,
                         "EXEC ",
                         5) == 0)
        {
            char command[128];

            char response[BUFFER_SIZE];

            /*
             * Extract command after "EXEC ".
             */
            if (sscanf(buffer + 5,
                       "%127s",
                       command) == 1)
            {
                execute_command(command,
                                response,
                                sizeof(response));

                send(client_fd,
                     response,
                     strlen(response),
                     0);

                printf("EXEC %s sent.\n",
                       command);
            }
            else
            {
                snprintf(response,
                         sizeof(response),
                         "ERR 004 UNKNOWN_EXEC SID:%s\n",
                         SESSION_ID);

                send(client_fd,
                     response,
                     strlen(response),
                     0);
            }
        }


        /*
         * UNKNOWN COMMAND
         */
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


    /*
     * Close connections.
     */
    close(client_fd);

    close(server_fd);

    printf("Agent stopped.\n");

    return 0;
}
