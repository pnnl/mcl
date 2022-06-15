#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <minos.h>
#include "utils.h"

#define WRITE_END   1
#define READ_END    0

int main(int argc, char** argv)
{
    mcl_banner("Shared Memory Functionality Test");

    pid_t producer, consumer;
    int fd[2];
    int err = pipe(fd);
    if(err == -1){
        fprintf(stderr, "Could not create pipe, error: %d\n", err);
        exit(-1);
    }

    producer = fork();
    if(producer==0)
    {
        dup2(fd[WRITE_END], STDOUT_FILENO);
        close(fd[READ_END]);
        close(fd[WRITE_END]);

        int log = open("mcl_shm_test_producer.log", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        dup2(log, STDERR_FILENO);
        close(log);

        argv[0] = "./mcl_shm_producer";
        execv("./mcl_shm_producer", argv);
        fprintf(stderr, "Failed to execute './mcl_shm_producer'\n");
        exit(1);
    }
    else
    { 
        consumer=fork();

        if(consumer==0)
        {
            dup2(fd[READ_END], STDIN_FILENO);
            close(fd[WRITE_END]);
            close(fd[READ_END]);

            int log = open("mcl_shm_test_consumer.log", O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
            dup2(log, STDERR_FILENO);
            close(log);

            argv[0] = "./mcl_shm_consumer";
            execv("./mcl_shm_consumer", argv);
            fprintf(stderr, "Failed to execute './mcl_shm_consumer'\n");
            exit(1);
        }
        else
        {
            int status;
            close(fd[READ_END]);
            close(fd[WRITE_END]);
            fprintf(stderr, "Waiting for consumer to exit.\n");
            waitpid(consumer, &status, 0);
            fprintf(stderr, "Waiting for producer to exit.\n");
            waitpid(producer, &status, 0);
        }
    }
}