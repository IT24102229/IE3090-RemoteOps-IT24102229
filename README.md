# IE3090 RemoteOps – IT24102229

## Student Information

- Registration Number: IT24102229
- Name: Bandara.G.R.S.S
- Module: IE3090 Network Programming
- Project: RemoteOps – Remote System Monitoring and Management Tool

## Personalisation

| Item | Value |
|---|---|
| Registration Number | IT24102229 |
| Agent Port | 9410 |
| Session ID | SID:9222 |
| Authentication Token | OPS-2229 |
| Agent | agent_229.c |
| Controller | controller_229.c |
| Makefile | Makefile_229 |
| Log File | remoteops_IT24102229.log |
| Storage | ./agentfiles/IT24102229/ |

## Project Description

RemoteOps is a TCP-based remote system monitoring and management
application consisting of an Agent and a Controller.

The Agent runs as the server and listens on TCP port 9410.
The Controller connects to the Agent and provides commands for
system monitoring, process listing, restricted command execution,
file transfer and UDP monitoring.

## Features

- TCP client-server communication
- Multiple simultaneous Controllers
- Authentication
- SYSINFO
- LISTPROC
- Restricted EXEC commands
- PUT file upload
- GET file download
- UDP monitoring
- Graceful QUIT
- Event logging
- Personalized session ID

## EXEC Whitelist

The following commands are supported:

- DATE
- UPTIME
- DISKFREE
- HOSTNAME
- WHOAMI

Other commands are rejected.

## Build

Compile the Agent:

    gcc agent_229.c -o agent_229 -pthread

Compile the Controller:

    gcc controller_229.c -o controller_229 -pthread

Or use the Makefile:

    make -f Makefile_229

## Running

### Start the Agent

    ./agent_229

The Agent listens on TCP port 9410.

### Start the Controller

In another terminal:

    ./controller_229

Authenticate using:

    AUTH OPS-2229

## Example Commands

    AUTH OPS-2229
    SYSINFO
    LISTPROC
    EXEC DATE
    EXEC UPTIME
    EXEC DISKFREE
    EXEC HOSTNAME
    EXEC WHOAMI

### File Upload

    PUT

The Controller prompts for the local and remote filenames.

### File Download

    GET

The Controller prompts for the remote and local filenames.

### UDP Monitoring

    MONITOR START 9500

To stop monitoring:

    MONITOR STOP

### Disconnect

    QUIT

## File Storage

Uploaded files are stored under:

    ./agentfiles/IT24102229/

## Logging

Agent activity is recorded in:

    remoteops_IT24102229.log

The log records important connection, authentication,
command and file-transfer events.

## Concurrency

The Agent uses a thread-per-Controller model using POSIX pthreads.
Each connected Controller is handled in a separate thread,
allowing multiple Controllers to operate simultaneously.

## Testing

The implementation was tested for:

- Authentication success and failure
- Commands before authentication
- SYSINFO
- LISTPROC
- Allowed EXEC commands
- Rejected EXEC commands
- PUT and GET
- File integrity using cmp/SHA-256
- UDP monitoring
- Graceful QUIT
- Five simultaneous Controllers

## Repository

GitHub repository:

https://github.com/IT24102229/IE3090-RemoteOps-IT24102229
