/*
 * wcl.c - Minimal wcl (wc -l equivalent) for EGOS-2000
 *
 * Contract:
 *  - Usage: wcl [FILE1] [FILE2] ...
 *  - For each file argument, read the entire file using dir_lookup() and
 *    file_read(), count newline ('\n') characters, and print a single
 *    integer: the total number of lines across all files followed by a newline.
 *  - Returns 0 on success. If a file is not found, prints a warning and
 *    continues with remaining files.
 *
 * Implementation notes and edge cases:
 *  - Files are read block-by-block. Each block is treated as a C string by
 *    appending a terminating '\0' at buf[BLOCK_SIZE]. We count '\n' bytes in
 *    each buffer until a short read or EOF is observed.
 *  - If a file does not end with a trailing newline, the last line will still
 *    be counted only if it contains a '\n'. This matches the behavior of
 *    counting newline characters (equivalent to wc -l semantics).
 *  - Large files spanning many blocks are supported by incrementing offset.
 *  - No host libc file APIs or Linux syscalls are used; all IO goes through the
 *    EGOS syscall wrappers.
 */

#include "app.h"
#include "disk.h"
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        INFO("usage: wcl [FILE1] [FILE2] ...");
        return -1;
    }

    int total = 0;
    for (int a = 1; a < argc; a++) {
        char* filename = argv[a];
        int file_ino = dir_lookup(workdir_ino, filename);
        if (file_ino < 0) {
            INFO("wcl: file %s not found", filename);
            continue;
        }

        char buf[BLOCK_SIZE + 1];
        uint offset = 0;
        while (1) {
            /* Read consecutive blocks until EOF (empty block) or short read */
            file_read(file_ino, offset, buf);
            buf[BLOCK_SIZE] = '\0';
            if (buf[0] == '\0') break;

            /* Count newline characters in this buffer. */
            for (int i = 0; i < BLOCK_SIZE && buf[i]; i++) {
                if (buf[i] == '\n') total++;
            }

            /* Stop on short read (last block). */
            if (strlen(buf) < BLOCK_SIZE) break;
            offset++;
        }
    }

    printf("%d\n", total);
    return 0;
}
