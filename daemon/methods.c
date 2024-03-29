#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include "methods.h"
#include <string.h>

#include <signal.h>
#include <stdlib.h>

#include "../shared/socket_encoding.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct service_info_t SERVICES[100];

service_t LAST_INDEX = 0;


struct service_create_args_t* deserialize_service_create_args(char* buffer, ssize_t params_size){
    struct service_create_args_t* args = malloc(sizeof(struct service_create_args_t));
    char* currentBufferPosition = buffer;
    memcpy(&args->supervisor, currentBufferPosition, sizeof(args->supervisor));
    currentBufferPosition += sizeof(args->supervisor);
    // Get first 4 bytes of buffer and convert to int
    int servicename_size = decode_length(currentBufferPosition);
    currentBufferPosition += sizeof(servicename_size);
    // Get servicename
    args->servicename = malloc(servicename_size + 1);
    memcpy(args->servicename, currentBufferPosition, servicename_size);
    args->servicename[servicename_size] = '\0';
    currentBufferPosition += servicename_size;
    // Get first 4 bytes of buffer and convert to int
    int programpath_size = decode_length(currentBufferPosition);
    currentBufferPosition += sizeof(programpath_size);
    // Get programpath
    args->program_path = malloc(programpath_size + 1);
    memcpy(args->program_path, currentBufferPosition, programpath_size);
    args->program_path[programpath_size] = '\0';
    currentBufferPosition += programpath_size;
    // Get flags
    memcpy(&args->flags, currentBufferPosition, sizeof(args->flags));
    currentBufferPosition += sizeof(args->flags);
    // Get argc
    memcpy(&args->argc, currentBufferPosition, sizeof(args->argc));
    currentBufferPosition += sizeof(args->argc);
    // Get argv
    args->argv = malloc(args->argc * sizeof(char*));
    for(int i = 0; i < args->argc; i++){
        // Get first 4 bytes of buffer and convert to int
        int arg_size = decode_length(currentBufferPosition);
        currentBufferPosition += sizeof(arg_size);
        // Get arg
        args->argv[i] = malloc(arg_size + 1);
        memcpy(args->argv[i], currentBufferPosition, arg_size);
        args->argv[i][arg_size] = '\0';
        currentBufferPosition += arg_size;
    }
    return args;
}

service_t service_create(
        const char * servicename,
        const char * program_path,
        const char ** argv,
        int argc,
        int flags
) {

    service_t new_service_id = -1;

    pthread_mutex_lock(&mutex);

    for(int i = 0; i < LAST_INDEX; i++){
        if(SERVICES[i].status == 0){
            new_service_id = i;
            break;
        }
    }

    if(new_service_id == -1)
        new_service_id = LAST_INDEX++;

    SERVICES[new_service_id].servicename = strdup(servicename);
    //    SERVICES[new_service_id].flags = flags;
    SERVICES[new_service_id].argc = argc;

    SERVICES[new_service_id].program_path = strdup(program_path);


    SERVICES[new_service_id].restart_times = (flags >> 16) & 0xF;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        free(SERVICES[new_service_id].servicename);
        return -1;
    }
    else if (pid > 0) {
        SERVICES[new_service_id].pid = pid;
        SERVICES[new_service_id].process_name = strdup(get_process_name_by_pid(pid));

        if (flags & SUPERVISOR_FLAGS_CREATESTOPPED) {
            kill(pid, SIGSTOP);
            SERVICES[new_service_id].flags = SUPERVISOR_FLAGS_CREATESTOPPED;

            SERVICES[new_service_id].status = SUPERVISOR_STATUS_PENDING;
        }
        else {
            SERVICES[new_service_id].flags = 0;
            SERVICES[new_service_id].status = SUPERVISOR_STATUS_RUNNING;
        }
        pthread_mutex_unlock(&mutex);
        return new_service_id;
    }
    else {
        pthread_mutex_unlock(&mutex);

        SERVICES[new_service_id].args = malloc((argc + 2) * sizeof(char*));
        SERVICES[new_service_id].args[0] = strdup(program_path);
        for (int i = 0; i < argc; i++) {
            SERVICES[new_service_id].args[i + 1] = strdup(argv[i]);
        }
        SERVICES[new_service_id].args[argc + 1] = NULL;
        execv(program_path, SERVICES[new_service_id].args);
        printf("Error executing program %s\n", program_path);
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("Current working dir: %s\n", cwd);
        } else {
            perror("getcwd");
        }
        exit(EXIT_FAILURE);
    }
}



int service_close(service_t service) {
    pthread_mutex_lock(&mutex);
    SERVICES[service].status = 0;
    free(SERVICES[service].servicename);
    free(SERVICES[service].process_name);
    SERVICES[service].supervisor = 0;
    SERVICES[service].pid = -1;
    free(SERVICES[service].args);
    SERVICES[service].restart_times = 0;
    SERVICES[service].flags = 0;
    free(SERVICES[service].program_path);
    pthread_mutex_unlock(&mutex);
    return 0;
}


service_t service_open(const char *servicename) {
    pthread_mutex_lock(&mutex);
    for(int i=0; i < LAST_INDEX; i++){
        if(strcmp(SERVICES[i].servicename, servicename) == 0){
            if( kill(SERVICES[i].pid, 0)  != 0 ){
                SERVICES[i].status = SUPERVISOR_STATUS_STOPPED;

                service_t new_service = service_create(SERVICES[i].servicename, SERVICES[i].program_path, (const char **) SERVICES[i].args,
                               SERVICES[i].argc, SERVICES[i].flags | SUPERVISOR_FLAGS_RESTARTTIMES(SERVICES[i].restart_times + 1));
                service_close(i);
                printf("Service %s restarted\n", servicename);
                return new_service;

            }
            else{
                char * process_name = get_process_name_by_pid(SERVICES[i].pid);
                if(strcmp(process_name, SERVICES[i].process_name) != 0){
                    SERVICES[i].status = SUPERVISOR_STATUS_STOPPED;

                    service_t new_service = service_create(SERVICES[i].servicename, SERVICES[i].program_path, (const char **) SERVICES[i].args,
                                                           SERVICES[i].argc, SERVICES[i].flags | SUPERVISOR_FLAGS_RESTARTTIMES(SERVICES[i].restart_times + 1));
                    service_close(i);
                    printf("Service %s restarted\n", servicename);
                    pthread_mutex_unlock(&mutex);
                    return new_service;
                }
            }
            pthread_mutex_unlock(&mutex);
            return i;
        }
    }
    pthread_mutex_unlock(&mutex);
    return -1;
}

int service_status(service_t service){
    pthread_mutex_lock(&mutex);
    int status = SERVICES[service].status;
    pthread_mutex_unlock(&mutex);
    return status;
}


int service_suspend(service_t service){
    pthread_mutex_lock(&mutex);
    if(SERVICES[service].status) {
        kill(SERVICES[service].pid, SIGSTOP);
        SERVICES[service].status = SUPERVISOR_STATUS_PENDING;
        pthread_mutex_unlock(&mutex);
        return SERVICES[service].status;
    }
    else{
        pthread_mutex_unlock(&mutex);
        return -1;
    }
}

int service_resume(service_t service){
    pthread_mutex_lock(&mutex);
    if(SERVICES[service].status) {
        kill(SERVICES[service].pid, SIGCONT);
        SERVICES[service].status = SUPERVISOR_STATUS_RUNNING;
        pthread_mutex_unlock(&mutex);
        return SERVICES[service].status;
    }
    else{
        pthread_mutex_unlock(&mutex);
        return -1;
    }
}

int service_cancel(service_t service){
    pthread_mutex_lock(&mutex);

    if(SERVICES[service].status) {
        kill(SERVICES[service].pid, SIGKILL);
        //SERVICES[service].status = SUPERVISOR_STATUS_STOPPED;
        pthread_mutex_unlock(&mutex);
        return SERVICES[service].status;
    }
    else{
        pthread_mutex_unlock(&mutex);
        return -1;
    }

}

int service_remove(service_t service){
    pthread_mutex_lock(&mutex);
    if(SERVICES[service].status) {
        pthread_mutex_unlock(&mutex);
        service_cancel(service);
        service_close(service);
        return 0;
    }
    else{
        pthread_mutex_unlock(&mutex);
        return -1;
    }
}

int supervisor_list(
        char ***service_names,
        unsigned int *count
) {
    if (service_names == NULL || count == NULL) {
        return -1;
    }

    *count = 0;

    pthread_mutex_lock(&mutex);

    for (int i = 0; i < MAX_SERVICES; i++) {
        if (SERVICES[i].status != 0) {
            (*count)++;
        }
    }

    if (*count == 0) {
        *service_names = NULL;
        pthread_mutex_unlock(&mutex);
        return 0;
    }

    *service_names = (char **)malloc(*count * sizeof(char *));
    if (*service_names == NULL) {
        perror("malloc");
        pthread_mutex_unlock(&mutex);
        exit(EXIT_FAILURE);
    }

    int index = 0;
    for (int i = 0; i < MAX_SERVICES; i++) {
        if (SERVICES[i].status != 0) {
            (*service_names)[index] = strdup(SERVICES[i].servicename);
            if ((*service_names)[index] == NULL) {
                perror("strdup");
                pthread_mutex_unlock(&mutex);
                exit(EXIT_FAILURE);
            }
            index++;
        }
    }

    pthread_mutex_unlock(&mutex);
    return 0;
}


int supervisor_freelist(char **service_names, int count) {

    if (service_names == NULL) {
        return -1;
    }

    pthread_mutex_lock(&mutex);
    for (int i = 0; i < count; i++) {
        free(service_names[i]);
    }

    free(service_names);
    pthread_mutex_unlock(&mutex);

    return 0;
}


const char* get_process_name_by_pid(const int pid)
{
    char* name = (char*)calloc(1024,sizeof(char));
    if(name){
        sprintf(name, "/proc/%d/cmdline",pid);
        FILE* f = fopen(name,"r");
        if(f){
            size_t size;
            size = fread(name, sizeof(char), 1024, f);
            if(size>0){
                if('\n'==name[size-1])
                    name[size-1]='\0';
            }
            fclose(f);
        }
    }
    return name;
}