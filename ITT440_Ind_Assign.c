
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define NUM_CHILDREN 3
#define MESSAGE_SIZE 256

int message_pipes[NUM_CHILDREN][2];
int sync_pipes[NUM_CHILDREN][2];

pid_t child_pids[NUM_CHILDREN];
volatile sig_atomic_t child_count = 0;

/* Handle Ctrl+C */
void handle_interrupt(int sig)
{
    (void)sig;

    const char msg[] =
        "\n\n[INTERRUPT] Ctrl+C detected!\n"
        "Parent process is terminating the child processes.\n"
        "Program interrupted successfully.\n";

    for (int i = 0; i < child_count; i++) {
        if (child_pids[i] > 0) {
            kill(child_pids[i], SIGTERM);
        }
    }

    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    _exit(130);
}

int main(void)
{
    char message[MESSAGE_SIZE];

    /* Register Ctrl+C handler */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_interrupt;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    printf("========================================\n");
    printf("     PROCESS INFORMATION SYSTEM\n");
    printf("========================================\n\n");

    printf("[ PARENT PROCESS ]\n");
    printf("Parent PID       : %ld\n", (long)getpid());

    printf("\nEnter a message: ");
    fflush(stdout);

    if (fgets(message, sizeof(message), stdin) == NULL) {
        printf("\nNo message entered. Program terminated.\n");
        return EXIT_FAILURE;
    }

    message[strcspn(message, "\n")] = '\0';

    /* Create message and synchronization pipes */
    for (int i = 0; i < NUM_CHILDREN; i++) {
        if (pipe(message_pipes[i]) == -1 ||
            pipe(sync_pipes[i]) == -1) {
            perror("pipe");
            return EXIT_FAILURE;
        }
    }

    /* Create child processes */
    for (int i = 0; i < NUM_CHILDREN; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            return EXIT_FAILURE;
        }

        if (pid == 0) {
            /* Child processes ignore Ctrl+C.
               The parent handles the interrupt. */
            signal(SIGINT, SIG_IGN);

            /* Close unused pipe ends */
            for (int j = 0; j < NUM_CHILDREN; j++) {
                close(message_pipes[j][1]);
                close(sync_pipes[j][1]);

                if (j != i) {
                    close(message_pipes[j][0]);
                    close(sync_pipes[j][0]);
                }
            }

            char received_message[MESSAGE_SIZE];

            /* Receive message from parent */
            ssize_t n = read(
                message_pipes[i][0],
                received_message,
                sizeof(received_message) - 1
            );

            close(message_pipes[i][0]);

            if (n <= 0) {
                close(sync_pipes[i][0]);
                _exit(EXIT_FAILURE);
            }

            received_message[n] = '\0';

            /* Wait until parent allows this child to print */
            char permission;
            ssize_t ready = read(
                sync_pipes[i][0],
                &permission,
                1
            );

            close(sync_pipes[i][0]);

            if (ready == 1) {
                printf("\n----------------------------------------\n");
                printf("[ CHILD PROCESS %d ]\n", i + 1);
                printf("Child PID        : %ld\n", (long)getpid());
                printf("Parent PID       : %ld\n", (long)getppid());
                printf("Message received : %s\n", received_message);
                printf("----------------------------------------\n");

                fflush(stdout);
                _exit(EXIT_SUCCESS);
            }

            _exit(EXIT_FAILURE);

        } else {
            child_pids[i] = pid;
            child_count++;
        }
    }

    /* Parent closes unused read ends */
    for (int i = 0; i < NUM_CHILDREN; i++) {
        close(message_pipes[i][0]);
        close(sync_pipes[i][0]);
    }

    printf("\nMessage sent to all child processes...\n");

    /* Send the message to every child */
    for (int i = 0; i < NUM_CHILDREN; i++) {
        size_t len = strlen(message);
        size_t sent = 0;

        while (sent < len) {
            ssize_t n = write(
                message_pipes[i][1],
                message + sent,
                len - sent
            );

            if (n <= 0) {
                perror("write");
                break;
            }

            sent += (size_t)n;
        }

        close(message_pipes[i][1]);
    }

    /* Release and wait for children one by one */
    for (int i = 0; i < NUM_CHILDREN; i++) {
        char permission = '1';

        if (write(sync_pipes[i][1], &permission, 1) != 1) {
            perror("Synchronization write");
        }

        close(sync_pipes[i][1]);

        waitpid(child_pids[i], NULL, 0);
    }

    printf("\nAll child processes have completed.\n");
    printf("Parent process exiting.\n");

    return EXIT_SUCCESS;
}
