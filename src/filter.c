#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * Helper function for pfact
 * Takes a filter value (m), an input values file descriptor (read_fd)
 * and an output values file descriptor (write_fd).
 * Stores all values stored in the input file that are NOT multiples of m
 * into the output file (filters out multiples of m)
 */
int filter(int m, int read_fd, int write_fd) {
    int read_int;
    int error = 0;
    
    // keep reading the input file until its end
    while (read(read_fd, &read_int, sizeof(int)) > 0) {
        if ((read_int % m) != 0) {
            if (write(write_fd, &read_int, sizeof(int)) == -1) {
                // error code gets set permanently if an integer cannot be written,
                // but loop continues
                error = 1;
            }
        }
    }

    return error;
}
