#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <pthread.h>

#define PORT 9410
#define BUFFER_SIZE 8192

#define AUTH_TOKEN "OPS-2229"
#define SESSION_ID "9222"

#define STORAGE_DIR "./agentfiles/IT24102229"
#define LOG_FILE "remoteops_IT24102229.log"

#define MAX_FILE_SIZE (10 * 1024 * 1024)

#define MONITOR_INTERVAL 2


/* UDP monitoring variables */

int monitor_running = 0;
pthread_t monitor_thread;

struct sockaddr_in monitor_address;


/* -------------------------------------------------- */
/* Send all bytes                                      */
/* -------------------------------------------------- */

int send_all(int sock_fd, const char *data, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(sock_fd,
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

int receive_line(int sock_fd, char *buffer, size_t buffer_size)
{
    size_t position = 0;

    while (position < buffer_size - 1)
    {
        char character;

        ssize_t received =
            recv(sock_fd, &character, 1, 0);

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


/* -------------------------------------------------- */
/* Receive exact number of bytes                      */
/* -------------------------------------------------- */

int receive_exact(int sock_fd,
                  FILE *file,
                  long filesize)
{
    char buffer[BUFFER_SIZE];

    long remaining = filesize;

    while (remaining > 0)
    {
        size_t amount = sizeof(buffer);

        if (remaining < (long)amount)
        {
            amount = (size_t)remaining;
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

        if (written != (size_t)received)
        {
            return -1;
        }

        remaining -= received;
    }

    return 0;
}


/* -------------------------------------------------- */
/* Logging                                             */
/* -------------------------------------------------- */

void write_log(const char *message)
{
    FILE *log_file = fopen(LOG_FILE, "a");

    if (log_file == NULL)
    {
        perror("Log file");
        return;
    }

    fprintf(log_file, "%s\n", message);

    fclose(log_file);
}


/* -------------------------------------------------- */
/* Get system information                             */
/* -------------------------------------------------- */

void get_sysinfo(char *response, size_t response_size)
{
    FILE *fp;

    double uptime = 0;
    double load_average = 0;

    long mem_total_kb = 0;
    long mem_available_kb = 0;


    /* Get uptime */

    fp = fopen("/proc/uptime", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &uptime);

        fclose(fp);
    }


    /* Get CPU load */

    fp = fopen("/proc/loadavg", "r");

    if (fp != NULL)
    {
        fscanf(fp, "%lf", &load_average);

        fclose(fp);
    }


    /* Get memory information */

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


/* -------------------------------------------------- */
/* Get process list                                   */
/* -------------------------------------------------- */

void get_process_list(char *response,
                      size_t response_size)
{
    FILE *fp;

    char line[256];

    size_t used = 0;


    fp = popen("ps -e -o pid=,comm=", "r");

    if (fp == NULL)
    {
        snprintf(response,
                 response_size,
                 "ERR 003 PROCESS_LIST_FAILED SID:%s\n",
                 SESSION_ID);

        return;
    }


    used = snprintf(response,
                    response_size,
                    "OK PROCS ");


    while (fgets(line, sizeof(line), fp) != NULL)
    {
        char pid[32];
        char name[128];

        line[strcspn(line, "\n")] = '\0';


        if (sscanf(line,
                   "%31s %127s",
                   pid,
                   name) == 2)
        {
            int written;


            if (used > strlen("OK PROCS "))
            {
                written =
                    snprintf(response + used,
                             response_size - used,
                             ",");

                if (written < 0 ||
                    (size_t)written >=
                    response_size - used)
                {
                    break;
                }

                used += written;
            }


            written =
                snprintf(response + used,
                         response_size - used,
                         "%s/%s",
                         pid,
                         name);


            if (written < 0 ||
                (size_t)written >=
                response_size - used)
            {
                break;
            }

            used += written;
        }
    }


    pclose(fp);


    if (used < response_size)
    {
        snprintf(response + used,
                 response_size - used,
                 " SID:%s\n",
                 SESSION_ID);
    }
}


/* -------------------------------------------------- */
/* Execute fixed whitelist commands                   */
/* -------------------------------------------------- */

void execute_command(char *command,
                     char *response,
                     size_t response_size)
{
    const char *system_command = NULL;

    FILE *fp;

    char output[512];


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
        snprintf(response,
                 response_size,
                 "ERR 002 COMMAND_NOT_ALLOWED SID:%s\n",
                 SESSION_ID);

        return;
    }


    fp = popen(system_command, "r");

    if (fp == NULL)
    {
        snprintf(response,
                 response_size,
                 "ERR 005 EXEC_FAILED SID:%s\n",
                 SESSION_ID);

        return;
    }


    memset(output, 0, sizeof(output));


    if (fgets(output,
              sizeof(output),
              fp) == NULL)
    {
        pclose(fp);

        snprintf(response,
                 response_size,
                 "OK EXEC_RESULT %s SID:%s\n",
                 command,
                 SESSION_ID);

        return;
    }


    output[strcspn(output, "\n")] = '\0';


    int status = pclose(fp);

    int exit_code = 0;


    if (WIFEXITED(status))
    {
        exit_code = WEXITSTATUS(status);
    }


    snprintf(response,
             response_size,
             "OK EXEC_RESULT %s %d %s SID:%s\n",
             command,
             exit_code,
             output,
             SESSION_ID);
}


/* -------------------------------------------------- */
/* Handle PUT                                         */
/* -------------------------------------------------- */

int handle_put(int client_fd,
               char *header)
{
    char filename[256];

    long filesize;


    if (sscanf(header,
               "PUT %255s %ld",
               filename,
               &filesize) != 2)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 004 INVALID_PUT SID:%s\n",
                 SESSION_ID);

        send_all(client_fd,
                 response,
                 strlen(response));

        return 0;
    }


    if (filesize < 0 ||
        filesize > MAX_FILE_SIZE)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 004 FILE_TOO_LARGE SID:%s\n",
                 SESSION_ID);

        send_all(client_fd,
                 response,
                 strlen(response));

        return 0;
    }


    /* Prevent path traversal */

    if (strstr(filename, "/") != NULL ||
        strstr(filename, "..") != NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 004 INVALID_FILENAME SID:%s\n",
                 SESSION_ID);

        send_all(client_fd,
                 response,
                 strlen(response));

        return 0;
    }


    mkdir("./agentfiles", 0755);

    mkdir(STORAGE_DIR, 0755);


    char filepath[512];


    snprintf(filepath,
             sizeof(filepath),
             "%s/%s",
             STORAGE_DIR,
             filename);


    FILE *file = fopen(filepath, "wb");


    if (file == NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 004 FILE_OPEN_FAILED SID:%s\n",
                 SESSION_ID);

        send_all(client_fd,
                 response,
                 strlen(response));

        return 0;
    }


    int result =
        receive_exact(client_fd,
                      file,
                      filesize);


    fclose(file);


    if (result < 0)
    {
        remove(filepath);

        return -1;
    }


    char response[BUFFER_SIZE];


    snprintf(response,
             sizeof(response),
             "OK FILE_RECEIVED %s SID:%s\n",
             filename,
             SESSION_ID);


    send_all(client_fd,
             response,
             strlen(response));


    printf("PUT received: %s (%ld bytes)\n",
           filename,
           filesize);


    write_log("PUT file received");


    return 0;
}


/* -------------------------------------------------- */
/* Handle GET                                         */
/* -------------------------------------------------- */

int handle_get(int client_fd,
               char *header)
{
    char filename[256];


    if (sscanf(header,
               "GET %255s",
               filename) != 1)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 004 INVALID_GET SID:%s\n",
                 SESSION_ID);

        send_all(client_fd,
                 response,
                 strlen(response));

        return 0;
    }


    /* Prevent path traversal */

    if (strstr(filename, "/") != NULL ||
        strstr(filename, "..") != NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 004 INVALID_FILENAME SID:%s\n",
                 SESSION_ID);

        send_all(client_fd,
                 response,
                 strlen(response));

        return 0;
    }


    char filepath[512];


    snprintf(filepath,
             sizeof(filepath),
             "%s/%s",
             STORAGE_DIR,
             filename);


    FILE *file = fopen(filepath, "rb");


    if (file == NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(response,
                 sizeof(response),
                 "ERR 005 FILE_NOT_FOUND SID:%s\n",
                 SESSION_ID);

        send_all(client_fd,
                 response,
                 strlen(response));

        return 0;
    }


    fseek(file, 0, SEEK_END);

    long filesize = ftell(file);

    fseek(file, 0, SEEK_SET);


    char response[BUFFER_SIZE];


    snprintf(response,
             sizeof(response),
             "OK FILE_SEND %s %ld SID:%s\n",
             filename,
             filesize,
             SESSION_ID);


    if (send_all(client_fd,
                 response,
                 strlen(response)) < 0)
    {
        fclose(file);

        return -1;
    }


    char buffer[BUFFER_SIZE];

    size_t bytes_read;


    while ((bytes_read =
            fread(buffer,
                  1,
                  sizeof(buffer),
                  file)) > 0)
    {
        if (send_all(client_fd,
                     buffer,
                     bytes_read) < 0)
        {
            fclose(file);

            return -1;
        }
    }


    fclose(file);


    printf("GET sent: %s (%ld bytes)\n",
           filename,
           filesize);


    write_log("GET file sent");


    return 0;
}


/* -------------------------------------------------- */
/* UDP monitoring thread                              */
/* -------------------------------------------------- */

void *monitor_function(void *arg)
{
    (void)arg;


    int udp_socket =
        socket(AF_INET,
               SOCK_DGRAM,
               0);


    if (udp_socket < 0)
    {
        perror("UDP socket");

        monitor_running = 0;

        return NULL;
    }


    while (monitor_running)
    {
        char message[BUFFER_SIZE];


        get_sysinfo(message,
                    sizeof(message));


        sendto(udp_socket,
               message,
               strlen(message),
               0,
               (struct sockaddr *)&monitor_address,
               sizeof(monitor_address));


        printf("UDP monitoring sent: %s",
               message);


        sleep(MONITOR_INTERVAL);
    }


    close(udp_socket);


    return NULL;
}


/* -------------------------------------------------- */
/* Main                                               */
/* -------------------------------------------------- */

int main()
{
    int server_fd;

    int client_fd;


    struct sockaddr_in server_addr;

    struct sockaddr_in client_addr;


    socklen_t client_len =
        sizeof(client_addr);


    char buffer[BUFFER_SIZE];


    /* Create TCP socket */

    server_fd =
        socket(AF_INET,
               SOCK_STREAM,
               0);


    if (server_fd < 0)
    {
        perror("Socket creation failed");

        return 1;
    }


    printf("TCP socket created successfully.\n");


    int reuse = 1;

    setsockopt(server_fd,
               SOL_SOCKET,
               SO_REUSEADDR,
               &reuse,
               sizeof(reuse));


    memset(&server_addr,
           0,
           sizeof(server_addr));


    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);


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


    if (listen(server_fd, 5) < 0)
    {
        perror("Listen failed");

        close(server_fd);

        return 1;
    }


    printf("Agent is listening...\n");


    client_fd =
        accept(server_fd,
               (struct sockaddr *)&client_addr,
               &client_len);


    if (client_fd < 0)
    {
        perror("Accept failed");

        close(server_fd);

        return 1;
    }


    printf("Controller connected successfully!\n");

    write_log("Controller connected");


    int authenticated = 0;


    while (1)
    {
        memset(buffer,
               0,
               sizeof(buffer));


        if (receive_line(client_fd,
                         buffer,
                         sizeof(buffer)) < 0)
        {
            printf("Controller disconnected.\n");

            write_log("Controller disconnected");

            break;
        }


        printf("Received: %s",
               buffer);


        /* ---------------- AUTH ---------------- */

        if (strncmp(buffer, "AUTH ", 5) == 0)
        {
            char expected_command[BUFFER_SIZE];


            snprintf(expected_command,
                     sizeof(expected_command),
                     "AUTH %s\n",
                     AUTH_TOKEN);


            if (strcmp(buffer,
                       expected_command) == 0)
            {
                authenticated = 1;


                char response[BUFFER_SIZE];


                snprintf(response,
                         sizeof(response),
                         "OK AUTHENTICATED SID:%s\n",
                         SESSION_ID);


                send_all(client_fd,
                         response,
                         strlen(response));


                printf("Authentication successful.\n");

                write_log("Authentication successful");
            }
            else
            {
                char response[BUFFER_SIZE];


                snprintf(response,
                         sizeof(response),
                         "ERR 001 AUTH_FAILED SID:%s\n",
                         SESSION_ID);


                send_all(client_fd,
                         response,
                         strlen(response));


                printf("Authentication failed.\n");

                write_log("Authentication failed");
            }
        }


        /* ---------------- NOT AUTHENTICATED ---------------- */

        else if (!authenticated)
        {
            char response[BUFFER_SIZE];


            snprintf(response,
                     sizeof(response),
                     "ERR 001 AUTH_FAILED SID:%s\n",
                     SESSION_ID);


            send_all(client_fd,
                     response,
                     strlen(response));
        }


        /* ---------------- SYSINFO ---------------- */

        else if (strcmp(buffer,
                        "SYSINFO\n") == 0)
        {
            char response[BUFFER_SIZE];


            get_sysinfo(response,
                        sizeof(response));


            send_all(client_fd,
                     response,
                     strlen(response));


            printf("SYSINFO sent.\n");
        }


        /* ---------------- LISTPROC ---------------- */

        else if (strcmp(buffer,
                        "LISTPROC\n") == 0)
        {
            char response[BUFFER_SIZE];


            get_process_list(response,
                             sizeof(response));


            send_all(client_fd,
                     response,
                     strlen(response));


            printf("LISTPROC sent.\n");
        }


        /* ---------------- EXEC ---------------- */

        else if (strncmp(buffer,
                         "EXEC ",
                         5) == 0)
        {
            char command[128];

            char response[BUFFER_SIZE];


            if (sscanf(buffer + 5,
                       "%127s",
                       command) == 1)
            {
                execute_command(command,
                                response,
                                sizeof(response));


                send_all(client_fd,
                         response,
                         strlen(response));


                printf("EXEC %s sent.\n",
                       command);
            }
            else
            {
                snprintf(response,
                         sizeof(response),
                         "ERR 002 COMMAND_NOT_ALLOWED SID:%s\n",
                         SESSION_ID);

                send_all(client_fd,
                         response,
                         strlen(response));
            }
        }


        /* ---------------- PUT ---------------- */

        else if (strncmp(buffer,
                         "PUT ",
                         4) == 0)
        {
            if (handle_put(client_fd,
                            buffer) < 0)
            {
                break;
            }
        }


        /* ---------------- GET ---------------- */

        else if (strncmp(buffer,
                         "GET ",
                         4) == 0)
        {
            if (handle_get(client_fd,
                            buffer) < 0)
            {
                break;
            }
        }


        /* ---------------- MONITOR START ---------------- */

        else if (strncmp(buffer,
                         "MONITOR START ",
                         14) == 0)
        {
            int udp_port;


            if (monitor_running)
            {
                char response[BUFFER_SIZE];


                snprintf(response,
                         sizeof(response),
                         "ERR 006 MONITOR_ALREADY_RUNNING SID:%s\n",
                         SESSION_ID);


                send_all(client_fd,
                         response,
                         strlen(response));

                continue;
            }


            if (sscanf(buffer + 14,
                       "%d",
                       &udp_port) == 1 &&
                udp_port > 0 &&
                udp_port <= 65535)
            {
                memset(&monitor_address,
                       0,
                       sizeof(monitor_address));


                monitor_address.sin_family =
                    AF_INET;


                monitor_address.sin_port =
                    htons(udp_port);


                monitor_address.sin_addr.s_addr =
                    client_addr.sin_addr.s_addr;


                monitor_running = 1;


                if (pthread_create(&monitor_thread,
                                   NULL,
                                   monitor_function,
                                   NULL) != 0)
                {
                    monitor_running = 0;


                    char response[BUFFER_SIZE];


                    snprintf(response,
                             sizeof(response),
                             "ERR 006 MONITOR_START_FAILED SID:%s\n",
                             SESSION_ID);


                    send_all(client_fd,
                             response,
                             strlen(response));

                    continue;
                }


                char response[BUFFER_SIZE];


                snprintf(response,
                         sizeof(response),
                         "OK MONITOR_STARTED SID:%s\n",
                         SESSION_ID);


                send_all(client_fd,
                         response,
                         strlen(response));


                printf("UDP monitoring started on port %d.\n",
                       udp_port);


                write_log("UDP monitoring started");
            }
            else
            {
                char response[BUFFER_SIZE];


                snprintf(response,
                         sizeof(response),
                         "ERR 006 INVALID_UDP_PORT SID:%s\n",
                         SESSION_ID);


                send_all(client_fd,
                         response,
                         strlen(response));
            }
        }


        /* ---------------- MONITOR STOP ---------------- */

        else if (strcmp(buffer,
                        "MONITOR STOP\n") == 0)
        {
            if (monitor_running)
            {
                monitor_running = 0;

                pthread_join(monitor_thread,
                             NULL);
            }


            char response[BUFFER_SIZE];


            snprintf(response,
                     sizeof(response),
                     "OK MONITOR_STOPPED SID:%s\n",
                     SESSION_ID);


            send_all(client_fd,
                     response,
                     strlen(response));


            printf("UDP monitoring stopped.\n");

            write_log("UDP monitoring stopped");
        }


        /* ---------------- QUIT ---------------- */

        else if (strcmp(buffer,
                        "QUIT\n") == 0)
        {
            if (monitor_running)
            {
                monitor_running = 0;

                pthread_join(monitor_thread,
                             NULL);
            }


            char response[BUFFER_SIZE];


            snprintf(response,
                     sizeof(response),
                     "OK BYE SID:%s\n",
                     SESSION_ID);


            send_all(client_fd,
                     response,
                     strlen(response));


            printf("Graceful disconnect requested.\n");

            write_log("Controller requested graceful disconnect");

            break;
        }


        /* ---------------- UNKNOWN COMMAND ---------------- */

        else
        {
            char response[BUFFER_SIZE];


            snprintf(response,
                     sizeof(response),
                     "ERR 002 COMMAND_NOT_ALLOWED SID:%s\n",
                     SESSION_ID);


            send_all(client_fd,
                     response,
                     strlen(response));
        }
    }


    if (monitor_running)
    {
        monitor_running = 0;

        pthread_join(monitor_thread,
                     NULL);
    }


    close(client_fd);

    close(server_fd);


    printf("Agent stopped.\n");


    write_log("Agent stopped");


    return 0;
}
