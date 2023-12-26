//
// Created by radu on 12/22/23.
//

#ifndef SUPERVISORLIBRARY_SERVICE_H
#define SUPERVISORLIBRARY_SERVICE_H

#include "supervisor.h"
#include <sys/wait.h>

typedef int supervisor_t;

#define SUPERVISOR_FLAGS_CREATESTOPPED 0x1
#define SUPERVISOR_FLAGS_RESTARTTIMES(times) ((times & 0xF) << 16)
#define SUPERVISOR_STATUS_RUNNING 0x1
#define SUPERVISOR_STATUS_PENDING 0x2
#define SUPERVISOR_STATUS_STOPPED 0x4
#define MAX_SERVICES 100
#define MAX_RESTART_TIMES 3


typedef struct {
    supervisor_t supervisor;
    char * servicename;
    int service_id;
    int pid;
    char ** args;
    int status;
    int restart_times;
    int flags;
} service_t;


//cerinta 2
service_t service_create(
        supervisor_t supervisor,
        const char *servicename,
        const char *program_path,
        const char **argv,
        int argc,
        int flags
);
int service_close(service_t service);

//cerinta 3
service_t service_open(
        supervisor_t supervisor,
        const char *servicename
);
int service_status(service_t service);

//cerina 4
int service_suspend(service_t service);
int service_resume(service_t service);

//cerinta 5
int service_cancel(service_t service);
int service_remove(service_t service);

//cerinta 6

service_t service_restart(supervisor_t supervisor, const char *servicename, const char *program_path, const char **argv, int argc, int flags);

#endif //SUPERVISORLIBRARY_SERVICE_H
