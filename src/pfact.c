#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <math.h>
#include "filter.h"

/*
 * error-checking wrapper function for write
 */
void Write(int fd, const void *buf, size_t count) {
    if (write(fd, buf, count) != count) {
        perror("write");
        exit(0);
    }
}

/*
 * error-checking wrapper function for read
 * returns the read's return value for looping
 */
ssize_t Read(int fd, void *buf, size_t count) {
    int return_value;
    if ((return_value = read(fd, buf, count)) == -1) {
        perror("read");
        exit(0);
    }

    return return_value;
}

/*
 * error-checking wrapper function for close
 */
void Close(int file_desc) {
    if (close(file_desc) == -1) {
        perror("close");
        exit(0);
    }
}

/*
 * error-checking wrapper function for malloc
 * returns the return value of malloc
 */
void *Malloc(size_t size) {
    void *return_value;

    if ((return_value = malloc(size)) == NULL) {
        perror("malloc");
        exit(0);
    }

    return return_value;
}

/*
 * sets up a malloc'd array of 2 ints and initializes them to 0
 * returns the pointer to the array
 */
int *set_up_factors() {
    int *factors = Malloc(2 * sizeof(int));
    factors[0] = 0;
    factors[1] = 0;

    return factors;
}

/*
 * Updates the array pointed at by factors with n's factors,
 * in relation to m;
 */
void update_factors(int n, int m, int *factors) {
    if ((m * m) == n) {
        factors[0] = m;
        factors[1] = m;
    } else if (n % m == 0) {
        if (factors[0] == 0) {
            factors[0] = m;
        } else {
            factors[1] = m;
        }
    }
}

/*
 * handles the child process case
 */
void handle_child_process(int io_index, int n, int next_filter, int io_pipes[][2], int factor_pipe[2], int final_output_pipe[2]) {
    int parent_pipe_read = io_pipes[io_index][0];
    int parent_pipe_write = io_pipes[io_index][1];
    int child_pipe_write;

    // close this process' writting end to the io_pipe shared with its parent
    Close(parent_pipe_write);
    // set up a io_pipe to be used with this process' child
    pipe(io_pipes[io_index + 1]);

    child_pipe_write = io_pipes[io_index + 1][1];

    if (io_index == 0) {
        // only the original process needs to have the factor_pipe
        // and final_output_pipe open for reading
        Close(factor_pipe[0]);
        Close(final_output_pipe[0]);
    }
    
    filter(next_filter, parent_pipe_read, child_pipe_write);
}

/*
 * relays the output from this process' parent, (or 2 to n)
 * to the input for this process' child
 */
void parent_relay_io(int n, int io_index, int io_pipes[][2]) {
    int input_output;
    int parent_pipe_read = io_pipes[io_index - 1][0];
    int child_pipe_read = io_pipes[io_index][0];
    int child_pipe_write = io_pipes[io_index][1];
    
    // parent processes do not need to read from the pipe
    // shared with children
    Close(child_pipe_read);

    if (io_index == 0) {
        // send integers from 2 to n to the pipe shared with
        // the child process
        for (int i = 2; i <= n; i++) {
            Write(child_pipe_write, &i, sizeof(int));
        }
    } else {
        // send the output from this process' parent process
        // to this process' child process
        while (Read(parent_pipe_read, &input_output, sizeof(int)) > 0) {
            Write(child_pipe_write, &input_output, sizeof(int));
        }
        Close(parent_pipe_read);
    }
    Close(child_pipe_write);
}

/*
 * handles the status code and exits with or returns
 * the number of filters created from that point on
 */
int handle_wait_status(int n, int status, int top_most_pid, int *factors, int *final_primes, int *factor_pipe, int *final_output_pipe) {
    int child_exit_code;
    
    if (WIFEXITED(status)) {
        child_exit_code = WEXITSTATUS(status);

        if (getpid() == top_most_pid) { 
            // factor_pipe and final_output_pipe writting end should only be 
            // closed by the original process so all other processes have it
            // open and ready to send to this process
            Close(factor_pipe[1]);
            Close(final_output_pipe[1]);

            // read the factors and the final list of prime numbers
            // found by the bottom-most child
            Read(factor_pipe[0], factors, 2 * sizeof(int));
            Read(final_output_pipe[0], final_primes, (n * sizeof(int)));
            
            Close(factor_pipe[0]); 
            Close(final_output_pipe[0]); 
            
            return 1 + child_exit_code;
        } else {
            exit(1 + child_exit_code);
        }
    } else {
        perror("wait");
        exit(0);
    }
}

/*
 * handles the parent process case
 */
int handle_parent_process(int io_index, int n, int top_most_pid, int *factors, int *final_primes, int io_pipes[][2], int factor_pipe[2], int final_output_pipe[2]) {
    int status;

    parent_relay_io(n, io_index, io_pipes);
 
    wait(&status);
    
    return handle_wait_status(n, status, top_most_pid, factors, final_primes, factor_pipe, final_output_pipe);
}

/*
 * handles the termination of the bottom-most child
 * sends the factors of n and the final list
 * of prime numbers that were found to the original
 * process
 */
int handle_termination(int return_code, int next_filter, int io_index, int *factors, int io_pipes[][2], int *factor_pipe, int *final_output_pipe) {
    int final_output;
    
    Close(io_pipes[io_index][1]);

    // the bottom-most child (newest) process sends the factors it found
    // to the top-most parent (original) process
    Write(factor_pipe[1], factors, 2 * sizeof(int));
    Close(factor_pipe[1]);

    // the bottom-most child process send the list of numbers
    // that were found from the final call to filter
    Write(final_output_pipe[1], &next_filter, sizeof(int));
    while (Read(io_pipes[io_index][0], &final_output, sizeof(int)) > 0) {
        Write(final_output_pipe[1], &final_output, sizeof(int));
    } 

    Close(io_pipes[io_index][0]);
    Close(final_output_pipe[1]); 
        
    exit(0);
}

/*
 * Sieve-like algorithm to find prime numbers.
 * Parents pipe filtered numbers down to children and
 * the last child pipes the factors found up to the top most parent (original process)
 */
int pfact(int n, int top_most_pid, int *factors, int *final_primes) {
    int fork_ret;
    int io_pipes[(int) sqrt(n) + 1][2];
    int factor_pipe[2];
    int final_output_pipe[2];
    int io_index = 0;
    int return_code = 0;
    int next_filter = 2; 

    pipe(io_pipes[0]);
    pipe(factor_pipe);
    pipe(final_output_pipe);

    while ((factors[1] == 0) && (next_filter * next_filter <= n)) { 
        update_factors(n, next_filter, factors);

        if (factors[1] != 0) {
            break;
        }
        
        if ((fork_ret = fork()) == 0) { 
            handle_child_process(io_index, n, next_filter, io_pipes, factor_pipe, final_output_pipe);

            io_index++;
                        
            Read(io_pipes[io_index][0], &next_filter, sizeof(int));
        } else if (fork_ret > 0) {
            return_code = handle_parent_process(io_index, n, top_most_pid, factors, final_primes, io_pipes, factor_pipe, final_output_pipe);
            break;
        } else {
            perror("fork");
            exit(0);
        }
    }
    
    if (getpid() == top_most_pid) {
        return return_code;
    }
    return handle_termination(return_code, next_filter, io_index, factors, io_pipes, factor_pipe, final_output_pipe);
}

/*
 * prints out the appropriate result message
 */
void print_results(int n, int *factors, int *final_primes, int *filters) {
    int factor1 = factors[0];
    int factor2 = factors[1];

    if (factor1 == 0) {
        printf("%d is prime\n", n);
    } else if (factor2 == 0) {
        factor2 = n / factor1;
        
        // check to see if factor2 calculated above is in
        // the list of prime numbers found by the final
        // call to filter
        for (int i = 0; i < n; i++) {
            if (final_primes[i] == factor2) {
                printf("%d %d %d\n", n, factor1, factor2);
                break;
            } else if (i + 1 == n) {
                printf("%d is not the product of two primes\n", n);
            }
        } 
    } else if (factor1 * factor2 == n) {
        printf("%d %d %d\n", n, factor1, factor2);
    } else {
        printf("%d is not the product of two primes\n", n);
    }
}

int main(int argc, char **argv) {
    int n, filters;
    int *final_primes;
    int *factors = set_up_factors();   

    if (argc != 2) {
        fprintf(stderr, "Usage:\n\tpfact n\n");
        exit(1);
    }
    
    n = strtol(argv[1], NULL, 10);

    if (errno == ERANGE || n < 0) {
        fprintf(stderr, "Usage:\n\tpfact n\n");
        exit(1);
    }

    final_primes = Malloc(n * sizeof(int));

    filters = pfact(n, getpid(), factors, final_primes);
     
    print_results(n, factors, final_primes, &filters);
    printf("Number of filters = %d\n", filters);

    free(factors);
    free(final_primes);

    return 0;
}
