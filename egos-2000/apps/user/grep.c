/*
 * grep.c - Minimal grep implementation for EGOS-2000
 *
 * Contract:
 *  - Usage: grep [PATTERN] [FILE]
 *  - Reads the entire file using the EGOS syscall wrapper dir_lookup() and
 *    file_read(). Does not use any host libc file APIs (fopen/fread/etc.).
 *  - Prints each line of the file that contains PATTERN followed by a newline.
 *  - Returns 0 on success. If the file is not found or usage is wrong, prints
 *    an informative message and returns -1.
 *
 * Implementation notes and edge cases:
 *  - Files are read block-by-block using file_read(file_ino, offset, buf).
 *    Each block has BLOCK_SIZE bytes; we append a terminating '\0' to make
 *    safe C string operations.
 *  - Lines may be split across blocks. This simple implementation searches
 *    only within each buffer. It prints any line fragment that contains the
 *    pattern entirely inside the block. For the assignment's text files and
 *    typical BLOCK_SIZE, this behavior is acceptable. A full robust grep would
 *    need to stitch fragments across block boundaries.
 *  - Empty file detection: file_read returns an empty block (buf[0] == '\0')
 *    to indicate EOF and we stop reading further.
 *  - No binary/file-encoding handling: files are assumed textual.
 */

#include "app.h"
#include "disk.h"
#include <string.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        INFO("usage: grep [PATTERN] [FILE]");
        return -1;
    }

    char* pattern = argv[1];
    char* filename = argv[2];

    /* Lookup file inode in current working directory. */
    int file_ino = dir_lookup(workdir_ino, filename);
    if (file_ino < 0) {
        INFO("grep: file %s not found", filename);
        return -1;
    }

    char buf[BLOCK_SIZE + 1];
    uint offset = 0;
    while (1) {
        /* Read one block from the file at 'offset' (block index). */
        file_read(file_ino, offset, buf);
        buf[BLOCK_SIZE] = '\0';

        /* If the block is empty (EOF), stop. */
        if (buf[0] == '\0') break;

        /* Process each line (or line fragment) inside this block. */
        char* line = buf;
        while (line && *line) {
            /* Find end-of-line within this buffer (or buffer end). */
            char* eol = line;
            while (*eol && *eol != '\n' && (eol - buf) < (int)BLOCK_SIZE) eol++;

            int len = eol - line;
            /* Copy to a temporary, null-terminated buffer to use strstr(). */
            char tmp[BLOCK_SIZE + 1];
            if (len > 0) memcpy(tmp, line, len);
            tmp[len] = '\0';

            if (strstr(tmp, pattern)) {
                /* Print the matching line fragment (safe because tmp is null-terminated). */
                printf("%s\n", tmp);
            }

            /* If we reached end of buffer or end-of-file marker, break out. */
            if (*eol == '\0' || (eol - buf) >= (int)BLOCK_SIZE) break;
            line = eol + 1;
        }

        /* If this was a short read (last block shorter than BLOCK_SIZE), stop. */
        if (strlen(buf) < BLOCK_SIZE) break;
        offset++;
    }

    return 0;
}
