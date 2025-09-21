#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    openlog("writer", 0, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Missing arguments. Usage: writer <file_path> <text_string>");
        return 1;
    }

    const char *file_path = argv[1];
    const char *text_string = argv[2];

    FILE *fp = fopen(file_path, "w");

    if (fputs(text_string, fp) == EOF) {
        syslog(LOG_ERR, "Error writing to file: %s", file_path);
        fclose(fp);
        return 1;
    }

    syslog(LOG_DEBUG, "Writing %s to %s", text_string, file_path);

    fclose(fp);
    return 0;
}