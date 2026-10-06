#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

/* Protect concurrent log writes */
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Per-client session */
typedef struct
{
    int client_fd;
    struct sockaddr_in client_addr;

    int authenticated;

    int monitor_running;
    pthread_t monitor_thread;
    struct sockaddr_in monitor_address;

} ClientSession;


/* Send all data */
int send_all(int sockfd, const void *buffer, size_t length)
{
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(
            sockfd,
            (const char *)buffer + total_sent,
            length - total_sent,
            0
        );

        if (sent <= 0)
        {
            return -1;
        }

        total_sent += sent;
    }

    return 0;
}


/* Receive until newline */
int receive_line(int sockfd, char *buffer, size_t size)
{
    size_t index = 0;

    while (index < size - 1)
    {
        char c;

        ssize_t received = recv(sockfd, &c, 1, 0);

        if (received <= 0)
        {
            return -1;
        }

        buffer[index++] = c;

        if (c == '\n')
        {
            break;
        }
    }

    buffer[index] = '\0';

    return 0;
}


/* Receive an exact number of bytes */
int receive_exact(int sockfd, void *buffer, size_t length)
{
    size_t total_received = 0;

    while (total_received < length)
    {
        ssize_t received = recv(
            sockfd,
            (char *)buffer + total_received,
            length - total_received,
            0
        );

        if (received <= 0)
        {
            return -1;
        }

        total_received += received;
    }

    return 0;
}


/* Write a message to the log */
void write_log(const char *message)
{
    pthread_mutex_lock(&log_mutex);

    FILE *log_file = fopen(LOG_FILE, "a");

    if (log_file != NULL)
    {
        fprintf(log_file, "%s\n", message);
        fclose(log_file);
    }

    pthread_mutex_unlock(&log_mutex);
}


/* Get system information */
void get_sysinfo(
    char *load,
    size_t load_size,
    long *memory_used,
    long *uptime
)
{
    FILE *file;

    double uptime_value = 0.0;

    file = fopen("/proc/uptime", "r");

    if (file != NULL)
    {
        fscanf(file, "%lf", &uptime_value);
        fclose(file);
    }

    *uptime = (long)uptime_value;

    file = fopen("/proc/loadavg", "r");

    if (file != NULL)
    {
        fscanf(file, "%s", load);
        fclose(file);
    }
    else
    {
        snprintf(load, load_size, "0.00");
    }

    long mem_total = 0;
    long mem_available = 0;

    file = fopen("/proc/meminfo", "r");

    if (file != NULL)
    {
        char line[256];

        while (fgets(line, sizeof(line), file))
        {
            if (sscanf(line, "MemTotal: %ld kB", &mem_total) == 1)
            {
                continue;
            }

            if (sscanf(line, "MemAvailable: %ld kB", &mem_available) == 1)
            {
                continue;
            }
        }

        fclose(file);
    }

    *memory_used = (mem_total - mem_available) / 1024;
}


/* Get running processes */
void get_process_list(char *buffer, size_t buffer_size)
{
    FILE *process_file;

    process_file = popen("ps -e -o pid=,comm=", "r");

    if (process_file == NULL)
    {
        snprintf(buffer, buffer_size, "ERROR");
        return;
    }

    size_t used = 0;
    char line[256];

    while (fgets(line, sizeof(line), process_file))
    {
        line[strcspn(line, "\n")] = '\0';

        int written = snprintf(
            buffer + used,
            buffer_size - used,
            "%s%s",
            used == 0 ? "" : ",",
            line
        );

        if (written < 0)
        {
            break;
        }

        if ((size_t)written >= buffer_size - used)
        {
            break;
        }

        used += written;
    }

    pclose(process_file);
}


/* Execute only allowed commands */
int execute_command(
    const char *command_name,
    char *output,
    size_t output_size,
    int *exit_code
)
{
    const char *command = NULL;

    if (strcmp(command_name, "DATE") == 0)
    {
        command = "date";
    }
    else if (strcmp(command_name, "UPTIME") == 0)
    {
        command = "uptime";
    }
    else if (strcmp(command_name, "DISKFREE") == 0)
    {
        command = "df -h .";
    }
    else if (strcmp(command_name, "HOSTNAME") == 0)
    {
        command = "hostname";
    }
    else if (strcmp(command_name, "WHOAMI") == 0)
    {
        command = "whoami";
    }
    else
    {
        return -1;
    }

    FILE *process = popen(command, "r");

    if (process == NULL)
    {
        return -2;
    }

    output[0] = '\0';

    char line[1024];

    while (fgets(line, sizeof(line), process))
    {
        if (strlen(output) + strlen(line) < output_size - 1)
        {
            strcat(output, line);
        }
    }

    int status = pclose(process);

    if (status == -1)
    {
        *exit_code = -1;
    }
    else
    {
        *exit_code = WEXITSTATUS(status);
    }

    output[strcspn(output, "\n")] = '\0';

    return 0;
}


/* Handle file upload */
int handle_put(ClientSession *session, char *request)
{
    char filename[256];
    long filesize;

    if (sscanf(request, "PUT %255s %ld", filename, &filesize) != 2)
    {
        char response[BUFFER_SIZE];

        snprintf(
            response,
            sizeof(response),
            "ERR 004 INVALID_PUT SID:%s\n",
            SESSION_ID
        );

        send_all(
            session->client_fd,
            response,
            strlen(response)
        );

        return -1;
    }

    if (filesize < 0 || filesize > MAX_FILE_SIZE)
    {
        char response[BUFFER_SIZE];

        snprintf(
            response,
            sizeof(response),
            "ERR 004 INVALID_FILE_SIZE SID:%s\n",
            SESSION_ID
        );

        send_all(
            session->client_fd,
            response,
            strlen(response)
        );

        return -1;
    }

    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL ||
        strchr(filename, '\\') != NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(
            response,
            sizeof(response),
            "ERR 004 INVALID_FILENAME SID:%s\n",
            SESSION_ID
        );

        send_all(
            session->client_fd,
            response,
            strlen(response)
        );

        return -1;
    }

    mkdir("./agentfiles", 0755);
    mkdir(STORAGE_DIR, 0755);

    char path[512];

    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        STORAGE_DIR,
        filename
    );

    FILE *file = fopen(path, "wb");

    if (file == NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(
            response,
            sizeof(response),
            "ERR 004 FILE_OPEN_FAILED SID:%s\n",
            SESSION_ID
        );

        send_all(
            session->client_fd,
            response,
            strlen(response)
        );

        return -1;
    }

    char buffer[4096];
    long remaining = filesize;

    while (remaining > 0)
    {
        size_t chunk =
            remaining > (long)sizeof(buffer)
            ? sizeof(buffer)
            : (size_t)remaining;

        if (receive_exact(
                session->client_fd,
                buffer,
                chunk
            ) != 0)
        {
            fclose(file);
            return -1;
        }

        fwrite(buffer, 1, chunk, file);

        remaining -= chunk;
    }

    fclose(file);

    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK FILE_RECEIVED %s SID:%s\n",
        filename,
        SESSION_ID
    );

    send_all(
        session->client_fd,
        response,
        strlen(response)
    );

    char log_message[512];

    snprintf(
        log_message,
        sizeof(log_message),
        "PUT received: %s (%ld bytes)",
        filename,
        filesize
    );

    write_log(log_message);

    return 0;
}


/* Handle file download */
int handle_get(ClientSession *session, char *request)
{
    char filename[256];

    if (sscanf(request, "GET %255s", filename) != 1)
    {
        char response[BUFFER_SIZE];

        snprintf(
            response,
            sizeof(response),
            "ERR 004 INVALID_GET SID:%s\n",
            SESSION_ID
        );

        send_all(
            session->client_fd,
            response,
            strlen(response)
        );

        return -1;
    }

    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL ||
        strchr(filename, '\\') != NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(
            response,
            sizeof(response),
            "ERR 004 INVALID_FILENAME SID:%s\n",
            SESSION_ID
        );

        send_all(
            session->client_fd,
            response,
            strlen(response)
        );

        return -1;
    }

    char path[512];

    snprintf(
        path,
        sizeof(path),
        "%s/%s",
        STORAGE_DIR,
        filename
    );

    FILE *file = fopen(path, "rb");

    if (file == NULL)
    {
        char response[BUFFER_SIZE];

        snprintf(
            response,
            sizeof(response),
            "ERR 005 FILE_NOT_FOUND SID:%s\n",
            SESSION_ID
        );

        send_all(
            session->client_fd,
            response,
            strlen(response)
        );

        return -1;
    }

    fseek(file, 0, SEEK_END);

    long filesize = ftell(file);

    fseek(file, 0, SEEK_SET);

    char response[BUFFER_SIZE];

    snprintf(
        response,
        sizeof(response),
        "OK FILE_SEND %s %ld SID:%s\n",
        filename,
        filesize,
        SESSION_ID
    );

    if (send_all(
            session->client_fd,
            response,
            strlen(response)
        ) != 0)
    {
        fclose(file);
        return -1;
    }

    char buffer[4096];

    size_t bytes_read;

    while ((bytes_read = fread(
                buffer,
                1,
                sizeof(buffer),
                file
            )) > 0)
    {
        if (send_all(
                session->client_fd,
                buffer,
                bytes_read
            ) != 0)
        {
            fclose(file);
            return -1;
        }
    }

    fclose(file);

    char log_message[512];

    snprintf(
        log_message,
        sizeof(log_message),
        "GET sent: %s (%ld bytes)",
        filename,
        filesize
    );

    write_log(log_message);

    return 0;
}


/* UDP monitoring thread */
void *monitor_function(void *argument)
{
    ClientSession *session =
        (ClientSession *)argument;

    int udp_socket =
        socket(AF_INET, SOCK_DGRAM, 0);

    if (udp_socket < 0)
    {
        return NULL;
    }

    while (session->monitor_running)
    {
        char load[64];

        long memory_used;
        long uptime;

        get_sysinfo(
            load,
            sizeof(load),
            &memory_used,
            &uptime
        );

        char message[BUFFER_SIZE];

        snprintf(
            message,
            sizeof(message),
            "OK SYSINFO %s %ld %ld SID:%s\n",
            load,
            memory_used,
            uptime,
            SESSION_ID
        );

        sendto(
            udp_socket,
            message,
            strlen(message),
            0,
            (struct sockaddr *)&session->monitor_address,
            sizeof(session->monitor_address)
        );

        sleep(MONITOR_INTERVAL);
    }

    close(udp_socket);

    return NULL;
}


/* Handle one Controller */
void *handle_client(void *argument)
{
    ClientSession *session =
        (ClientSession *)argument;

    char client_ip[INET_ADDRSTRLEN];

    inet_ntop(
        AF_INET,
        &session->client_addr.sin_addr,
        client_ip,
        sizeof(client_ip)
    );

    printf(
        "Controller connected from %s:%d\n",
        client_ip,
        ntohs(session->client_addr.sin_port)
    );

    char log_message[512];

    snprintf(
        log_message,
        sizeof(log_message),
        "Controller connected from %s:%d",
        client_ip,
        ntohs(session->client_addr.sin_port)
    );

    write_log(log_message);

    char buffer[BUFFER_SIZE];

    while (1)
    {
        int result =
            receive_line(
                session->client_fd,
                buffer,
                sizeof(buffer)
            );

        if (result != 0)
        {
            printf(
                "Controller disconnected: %s:%d\n",
                client_ip,
                ntohs(session->client_addr.sin_port)
            );

            break;
        }

        printf(
            "[%s:%d] Received: %s",
            client_ip,
            ntohs(session->client_addr.sin_port),
            buffer
        );

        /* Authentication */
        if (strncmp(buffer, "AUTH ", 5) == 0)
        {
            char token[256];

            sscanf(
                buffer + 5,
                "%255s",
                token
            );

            if (strcmp(token, AUTH_TOKEN) == 0)
            {
                session->authenticated = 1;

                char response[BUFFER_SIZE];

                snprintf(
                    response,
                    sizeof(response),
                    "OK AUTHENTICATED SID:%s\n",
                    SESSION_ID
                );

                send_all(
                    session->client_fd,
                    response,
                    strlen(response)
                );

                write_log("Controller authenticated");

                printf(
                    "[%s:%d] Authentication successful\n",
                    client_ip,
                    ntohs(session->client_addr.sin_port)
                );
            }
            else
            {
                char response[BUFFER_SIZE];

                snprintf(
                    response,
                    sizeof(response),
                    "ERR 001 AUTH_FAILED SID:%s\n",
                    SESSION_ID
                );

                send_all(
                    session->client_fd,
                    response,
                    strlen(response)
                );

                write_log("Authentication failed");
            }

            continue;
        }

        /* All other commands require authentication */
        if (!session->authenticated)
        {
            char response[BUFFER_SIZE];

            snprintf(
                response,
                sizeof(response),
                "ERR 001 AUTH_REQUIRED SID:%s\n",
                SESSION_ID
            );

            send_all(
                session->client_fd,
                response,
                strlen(response)
            );

            continue;
        }

        /* SYSINFO */
        if (strcmp(buffer, "SYSINFO\n") == 0)
        {
            char load[64];

            long memory_used;
            long uptime;

            get_sysinfo(
                load,
                sizeof(load),
                &memory_used,
                &uptime
            );

            char response[BUFFER_SIZE];

            snprintf(
                response,
                sizeof(response),
                "OK SYSINFO %s %ld %ld SID:%s\n",
                load,
                memory_used,
                uptime,
                SESSION_ID
            );

            send_all(
                session->client_fd,
                response,
                strlen(response)
            );

            continue;
        }

        /* LISTPROC */
        if (strcmp(buffer, "LISTPROC\n") == 0)
        {
            char processes[BUFFER_SIZE];

            get_process_list(
                processes,
                sizeof(processes)
            );

            char response[BUFFER_SIZE];

            snprintf(
                response,
                sizeof(response),
                "OK PROCS %s SID:%s\n",
                processes,
                SESSION_ID
            );

            send_all(
                session->client_fd,
                response,
                strlen(response)
            );

            continue;
        }

        /* EXEC */
        if (strncmp(buffer, "EXEC ", 5) == 0)
        {
            char command_name[256];

            sscanf(
                buffer + 5,
                "%255s",
                command_name
            );

            char output[4096];

            int exit_code;

            int result =
                execute_command(
                    command_name,
                    output,
                    sizeof(output),
                    &exit_code
                );

            if (result == -1)
            {
                char response[BUFFER_SIZE];

                snprintf(
                    response,
                    sizeof(response),
                    "ERR 003 EXEC_NOT_ALLOWED SID:%s\n",
                    SESSION_ID
                );

                send_all(
                    session->client_fd,
                    response,
                    strlen(response)
                );
            }
            else if (result == -2)
            {
                char response[BUFFER_SIZE];

                snprintf(
                    response,
                    sizeof(response),
                    "ERR 003 EXEC_FAILED SID:%s\n",
                    SESSION_ID
                );

                send_all(
                    session->client_fd,
                    response,
                    strlen(response)
                );
            }
            else
            {
                char response[BUFFER_SIZE];

                snprintf(
                    response,
                    sizeof(response),
                    "OK EXEC_RESULT %s %d %s SID:%s\n",
                    command_name,
                    exit_code,
                    output,
                    SESSION_ID
                );

                send_all(
                    session->client_fd,
                    response,
                    strlen(response)
                );
            }

            continue;
        }

        /* PUT */
        if (strncmp(buffer, "PUT ", 4) == 0)
        {
            handle_put(
                session,
                buffer
            );

            continue;
        }

        /* GET */
        if (strncmp(buffer, "GET ", 4) == 0)
        {
            handle_get(
                session,
                buffer
            );

            continue;
        }

        /* Start monitoring */
        if (strncmp(buffer, "MONITOR START ", 14) == 0)
        {
            int udp_port;

            if (sscanf(
                    buffer + 14,
                    "%d",
                    &udp_port
                ) != 1 ||
                udp_port < 1 ||
                udp_port > 65535)
            {
                char response[BUFFER_SIZE];

                snprintf(
                    response,
                    sizeof(response),
                    "ERR 004 INVALID_UDP_PORT SID:%s\n",
                    SESSION_ID
                );

                send_all(
                    session->client_fd,
                    response,
                    strlen(response)
                );

                continue;
            }

            if (session->monitor_running)
            {
                char response[BUFFER_SIZE];

                snprintf(
                    response,
                    sizeof(response),
                    "ERR 004 MONITOR_ALREADY_RUNNING SID:%s\n",
                    SESSION_ID
                );

                send_all(
                    session->client_fd,
                    response,
                    strlen(response)
                );

                continue;
            }

            memset(
                &session->monitor_address,
                0,
                sizeof(session->monitor_address)
            );

            session->monitor_address.sin_family =
                AF_INET;

            session->monitor_address.sin_port =
                htons(udp_port);

            session->monitor_address.sin_addr =
                session->client_addr.sin_addr;

            session->monitor_running = 1;

            if (pthread_create(
                    &session->monitor_thread,
                    NULL,
                    monitor_function,
                    session
                ) != 0)
            {
                session->monitor_running = 0;

                char response[BUFFER_SIZE];

                snprintf(
                    response,
                    sizeof(response),
                    "ERR 004 MONITOR_START_FAILED SID:%s\n",
                    SESSION_ID
                );

                send_all(
                    session->client_fd,
                    response,
                    strlen(response)
                );

                continue;
            }

            char response[BUFFER_SIZE];

            snprintf(
                response,
                sizeof(response),
                "OK MONITOR_STARTED SID:%s\n",
                SESSION_ID
            );

            send_all(
                session->client_fd,
                response,
                strlen(response)
            );

            write_log("UDP monitoring started");

            continue;
        }

        /* Stop monitoring */
        if (strcmp(buffer, "MONITOR STOP\n") == 0)
        {
            if (!session->monitor_running)
            {
                char response[BUFFER_SIZE];

                snprintf(
                    response,
                    sizeof(response),
                    "ERR 004 MONITOR_NOT_RUNNING SID:%s\n",
                    SESSION_ID
                );

                send_all(
                    session->client_fd,
                    response,
                    strlen(response)
                );

                continue;
            }

            session->monitor_running = 0;

            pthread_join(
                session->monitor_thread,
                NULL
            );

            char response[BUFFER_SIZE];

            snprintf(
                response,
                sizeof(response),
                "OK MONITOR_STOPPED SID:%s\n",
                SESSION_ID
            );

            send_all(
                session->client_fd,
                response,
                strlen(response)
            );

            write_log("UDP monitoring stopped");

            continue;
        }

        /* Graceful disconnect */
        if (strcmp(buffer, "QUIT\n") == 0)
        {
            if (session->monitor_running)
            {
                session->monitor_running = 0;

                pthread_join(
                    session->monitor_thread,
                    NULL
                );
            }

            char response[BUFFER_SIZE];

            snprintf(
                response,
                sizeof(response),
                "OK BYE SID:%s\n",
                SESSION_ID
            );

            send_all(
                session->client_fd,
                response,
                strlen(response)
            );

            write_log(
                "Controller requested graceful disconnect"
            );

            printf(
                "Controller disconnected gracefully: %s:%d\n",
                client_ip,
                ntohs(session->client_addr.sin_port)
            );

            break;
        }

        /* Unknown command */
        {
            char response[BUFFER_SIZE];

            snprintf(
                response,
                sizeof(response),
                "ERR 002 UNKNOWN_COMMAND SID:%s\n",
                SESSION_ID
            );

            send_all(
                session->client_fd,
                response,
                strlen(response)
            );
        }
    }

    if (session->monitor_running)
    {
        session->monitor_running = 0;

        pthread_join(
            session->monitor_thread,
            NULL
        );
    }

    close(session->client_fd);

    free(session);

    return NULL;
}


/* Start the Agent */
int main()
{
    int server_fd;

    struct sockaddr_in server_addr;

    server_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    int option = 1;

    setsockopt(
        server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &option,
        sizeof(option)
    );

    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);

    if (bind(
            server_fd,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)
        ) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf(
        "RemoteOps Agent listening on port %d...\n",
        PORT
    );

    printf(
        "Agent supports multiple simultaneous Controllers.\n"
    );

    write_log("Agent started");

    /* Accept Controllers continuously */
    while (1)
    {
        struct sockaddr_in client_addr;

        socklen_t client_length =
            sizeof(client_addr);

        int client_fd =
            accept(
                server_fd,
                (struct sockaddr *)&client_addr,
                &client_length
            );

        if (client_fd < 0)
        {
            perror("accept");
            continue;
        }

        ClientSession *session =
            malloc(sizeof(ClientSession));

        if (session == NULL)
        {
            perror("malloc");

            close(client_fd);

            continue;
        }

        memset(
            session,
            0,
            sizeof(ClientSession)
        );

        session->client_fd =
            client_fd;

        session->client_addr =
            client_addr;

        session->authenticated =
            0;

        session->monitor_running =
            0;

        pthread_t client_thread;

        if (pthread_create(
                &client_thread,
                NULL,
                handle_client,
                session
            ) != 0)
        {
            perror("pthread_create");

            close(client_fd);

            free(session);

            continue;
        }

        pthread_detach(client_thread);
    }

    close(server_fd);

    return 0;
}
