#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#define MAX_PATH_LEN 1024
#define INTERACTIVE_MODE 1
#define BATCH_MODE 0

#ifdef DEBUG
    #define DEBUG_LOG(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
    #define DEBUG_LOG(fmt, ...) /* nothing */
#endif

char *input = NULL; // Declared as global to be able to freed inside local function

/* Directly called by main*/
char **create_path(void) {
    char **path = malloc(2 * sizeof(char *));
    if(!path) {
        perror("malloc");
        return NULL; // return to the caller
    }
    path[0] = strdup("/bin"); // to be compatible with add_path, which calls free
    if(!path[0]) {
        perror("strdup");
        free(path);
        return NULL;
    }
    path[1] = NULL;
    return path;
}

void destroy_path(char ***p_path) {
    for(char **p = (*p_path); *p != NULL; p++) {
        free(*p);
    }
    free(*p_path);
    *p_path = NULL;
}

int set_input(int argc, char **argv, int *p_mode, FILE **p_fpin) {
    if(argc > 2) return -1;

    if(argc == 1) { 
        fputs("wish> ", stdout);
        *p_mode = INTERACTIVE_MODE;
        *p_fpin = stdin;
    } else if(argc == 2){
        *p_mode = BATCH_MODE;
        *p_fpin = fopen(argv[1], "r");
        if(!(*p_fpin)) {
            // perror("fopen");
            return -1;
        }
    }
    return 0;
}

int create_parsed_commands(char *input, char ***p_commands, int *p_command_num) {
    input[strcspn(input, "\n")] = '\0';
    const char *delim = "&";
    char *token = NULL;

    int size = 4;
    int used = 0;
    *p_commands = malloc(size * sizeof(char *));
    if(!(*p_commands)) {
        perror("malloc");
        return -1;
    }

    while((token = strsep(&input, delim)) != NULL) {
        if(token[0] == '\0')
            continue;

        if(used >= size - 1) {
            size *= 2;
            char **tmp = realloc(*p_commands, size * sizeof(char *));
            if(!tmp) {
                perror("realloc");
                while(used--)
                    free((*p_commands)[used]);
                free(*p_commands);
                return -1;
            } else {
                *p_commands = tmp;
            }
        }
        (*p_commands)[used++] = token;
        (*p_commands)[used] = NULL;
    }
    *p_command_num = used;
    return 0;
}

void destroy_parsed_commands(char **commands) {
    free(commands);
}

/* Called by execute_commands */
int create_parsed_args(char* command, char ***p_myargv, int *p_myargc) {
    command[strcspn(command, "\n")] = '\0';
    if(!command) return -1;

    // dynamic array structure
    int size = 4;
    int used = 0;
    *p_myargv = malloc(size * sizeof(char *));
    if(!(*p_myargv)) {
        perror("malloc");
        return -1;
    }

    const char *delim = " ";
    char *token = NULL;
    while((token = strsep(&command, delim)) != NULL) {
        if(token[0] == '\0')
            continue;
        // insert into dynamic array 
        if(used >= size-1) { // keep one space for 'NULL'
            size *= 2;
            char **tmp = realloc(*p_myargv, size * sizeof(char *));
            if(!tmp) {
                perror("realloc");
                while (used--) 
                    free((*p_myargv)[used]);
                free(*p_myargv);
                return -1;
            } else {
                *p_myargv = tmp;
            }
        }
        (*p_myargv)[used++] = token;
        (*p_myargv)[used] = NULL;
    }
    *p_myargc = used;
    return 0;
}

void destroy_parsed_args(char **myargv) {
    free(myargv);
}

bool is_internal_command(char *command) {
    return (strcmp(command, "exit") == 0) || 
           (strcmp(command, "path") == 0) ||
           (strcmp(command, "cd") == 0);
}

// Internal commands
int add_path(int myargc, char **myargv, char*** p_path) {
    for(char **p = *p_path; *p != NULL; p++) {
        DEBUG_LOG("free: %s\n", *p);
        free(*p);
    }
    free(*p_path);
    *p_path = malloc(myargc * sizeof(char *));
    if(!(*p_path)) {
        perror("malloc");
        return -1;
    }

    for(int i = 1; i < myargc; i++) {
        (*p_path)[i-1] = strdup(myargv[i]);
    }
    (*p_path)[myargc-1] = NULL;
    return 0;
}

int cd(char **myargv) {
    if(chdir(myargv[1]) == -1) {
        perror("chdir");
        return -1;
    }
    return 0;
}

// External commands
int get_fd(char ***p_myargv, int *p_fdin, int *p_fdout) {
    *p_fdin = -1;
    *p_fdout = -1;
    char **left = *p_myargv;
    for(char **right = *p_myargv; *right != NULL; right++) {
        if(strcmp(*right, "<") == 0) {
            *right = NULL;
            right++;
            if((*p_fdin = open(*right, O_RDONLY)) == -1) {
                perror("open for reading");
                return -1;
            }
            *right = NULL;
        } else if(strcmp(*right, ">") == 0) {
            *right = NULL;
            right++;
            if((*p_fdout = open(*right, O_WRONLY | O_TRUNC | O_CREAT, 0644)) == -1) {
                perror("open for writing");
                return -1;
            }
            *right = NULL;
        } else {
            *left = *right;
            left++;
        }
    }
    *left = NULL; // terminate myargv with NULL
    return 0;
}

char *find_executable(char *command, char **path) {
    char *executable_path = malloc(MAX_PATH_LEN * sizeof(char)); // TODO: can this be more robust?
    if (executable_path == NULL) {
        perror("malloc");
        return NULL;
    }
    memset(executable_path, 0, MAX_PATH_LEN);

    for(char **p = path; *p != NULL; p++) {
        // concate p/command and reset buffer for each new path p
        strcpy(executable_path, *p);
        strcat(executable_path, "/");
        strcat(executable_path, command);
        // check if the shell can execute the binary at executable_path
        if(access(executable_path, F_OK | X_OK) == -1)
            continue; // not exist or not executable
        else
            return executable_path;
    }
    free(executable_path);
    return NULL;
}

int execute_commands(char **commands, int command_num, char ***p_path) {
    DEBUG_LOG("Execute command_num: %d\n", command_num);
    pid_t pids[command_num];
    int external_command_num = 0; // external_command_num <= command_num
    for(int i = 0; i < command_num; i++) {
        /* Arguments parsing*/
        char **myargv = NULL;
        int myargc = 0;
        if(create_parsed_args(commands[i], &myargv, &myargc) == -1) {
            DEBUG_LOG("Failed to parse_args.\n");
            // If `create_parsed_args` fails, it will ensure that any allocated memory is freed internally
            continue;
        }
        if(myargc == 0) {
            DEBUG_LOG("Receive command full of spaces");
            destroy_parsed_args(myargv);
            continue;
        }
        DEBUG_LOG("myargc: %d  command: %s\n", myargc, myargv[0]);

        /* Execution */
        if(is_internal_command(myargv[0])) {
            /* Internal commands */
            if(strcmp(myargv[0], "exit") == 0) {
                if(myargc > 1) {
                    DEBUG_LOG("exit received %d commands(should be 2), continued.\n", myargc);
                } else {
                    destroy_parsed_args(myargv);
                    destroy_parsed_commands(commands);
                    free(input);
                    destroy_path(p_path);
                    exit(EXIT_SUCCESS);
                }
            } else if(strcmp(myargv[0], "cd") == 0) {
                if(myargc != 2) {
                    DEBUG_LOG("cd received %d commands(should be 2), continued.\n", myargc);
                } else if(cd(myargv) == -1) {
                    DEBUG_LOG("cd failed, continued.\n");
                }
            } else if(strcmp(myargv[0], "path") == 0) {
                DEBUG_LOG("current path:\n");
                for(char **p = *p_path; *p != NULL; p++) {
                    DEBUG_LOG("%s\n", *p);
                }
                if(add_path(myargc, myargv, p_path) == -1) {
                    DEBUG_LOG("add_path failed, continued.\n");
                }
                DEBUG_LOG("after add_path:\n");
                for(char **p = *p_path; *p != NULL; p++) {
                    DEBUG_LOG("%s\n", *p);
                }
            }
        } else {
            pids[external_command_num] = fork();
            if(pids[external_command_num] == 0) {
                char *executable_path = find_executable(myargv[0], *p_path);
                if (!executable_path) {
                    DEBUG_LOG("[Child %d] Command not found: %s. Exiting\n", getpid(), myargv[0]);
                    _exit(EXIT_FAILURE);
                }
                // redirection
                int fdin, fdout;
                if(get_fd(&myargv, &fdin, &fdout) == -1) {
                    DEBUG_LOG("[Child %d] get_fd failed. Exiting\n", getpid());
                    _exit(EXIT_FAILURE);
                }
                DEBUG_LOG("[Child %d] fdin: %d  fdout: %d\n", getpid(), fdin, fdout);
                for(char **p = myargv; *p != NULL; p++)
                    DEBUG_LOG("[Child %d] %s\n", getpid(), *p);

                if(fdin != -1) { // If redirection required
                    if(dup2(fdin, STDIN_FILENO) == -1) {
                        perror("dup2");
                        _exit(EXIT_FAILURE);
                    }
                }
                if(fdout != -1) { // If redirection required
                    if(dup2(fdout, STDOUT_FILENO) == -1) {
                        perror("dup2");
                        _exit(EXIT_FAILURE);
                    }
                }
                execv(executable_path, myargv);
                perror("execv");  // execv only returns on error
                free(executable_path);
                _exit(EXIT_FAILURE);
            } else if(pids[external_command_num] < 0) {
                perror("fork");
                destroy_parsed_commands(commands);
                destroy_parsed_args(myargv);
                return -1;
            } else {
                external_command_num++; // parent keep the count
            }
        }
        destroy_parsed_args(myargv);
    }
    int status;
    for(int i = 0; i < external_command_num; i++) {
        if(pids[i] > 0) { // only wait for successful fork
            waitpid(pids[i], &status, 0);
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    /* Path initialization */
    char **path = create_path();
    if(!path) {
        DEBUG_LOG("Failed to create path. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    /* Set the interactive/batch mode and file pointer */
    int mode;
    FILE *fpin = NULL;
    if(set_input(argc, argv, &mode, &fpin) == -1) {
        DEBUG_LOG("Failed to set_input. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    /* Get input from fpin */
    size_t size = 0;
    while(getline(&input, &size, fpin) > 0) {
        if(mode == INTERACTIVE_MODE)
            fputs("wish> ", stdout);

        char **commands = NULL;
        int command_num = 0;
        if(create_parsed_commands(input, &commands, &command_num) == -1) {
            DEBUG_LOG("Failed to parse_commands.\n");
            // If `create_parsed_commands` fails, it will ensure that any allocated memory is freed internally
            continue;
        }
        if(execute_commands(commands, command_num, &path) == -1) {
            DEBUG_LOG("Failed to execute_commands.\n");
            // If `execute_commands` fails, it will ensure that any allocated memory is freed internally
            continue;
        }
        destroy_parsed_commands(commands);
    }
    // getline return -1 so check if it's error or EOF reached
    if(ferror(fpin)) {
        perror("getline");
        free(input);
        destroy_path(&path);
        exit(EXIT_FAILURE);
    }
    if(feof(fpin)) { // There should not be any memory allocated previously when executing this line (commands, myargv)
        DEBUG_LOG("Success: EOF reached, Exiting\n");
        free(input);
        destroy_path(&path);
        return 0;
    }
}