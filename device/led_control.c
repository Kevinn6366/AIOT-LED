/* Linux userspace controller for the PS LED path in the course diagram. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s COMMAND_FIFO EXPORTED_GPIO_VALUE\n", argv[0]);
        return EXIT_FAILURE;
    }
    /* The operator selects the verified PS GPIO and sets its direction. */
    int gpio = open(argv[2], O_WRONLY);
    if (gpio < 0) {
        perror("open GPIO value");
        return EXIT_FAILURE;
    }
    if (mkfifo(argv[1], 0600) < 0 && errno != EEXIST) {
        perror("mkfifo");
        return EXIT_FAILURE;
    }
    /* Keep a writer open so short-lived HTTP writers do not cause EOF. */
    int fd = open(argv[1], O_RDWR | O_NOFOLLOW);
    struct stat info;
    if (fd < 0 || fstat(fd, &info) < 0 || !S_ISFIFO(info.st_mode)) {
        fprintf(stderr, "Cannot open command FIFO\n");
        return EXIT_FAILURE;
    }
    FILE *commands = fdopen(fd, "r");
    if (!commands) {
        perror("fdopen");
        return EXIT_FAILURE;
    }
    char line[32];
    while (fgets(line, sizeof(line), commands)) {
        if (!strchr(line, '\n')) {
            int ch;
            while ((ch = fgetc(commands)) != '\n' && ch != EOF) {}
            continue;
        }
        if (strcmp(line, "1\n") && strcmp(line, "0\n")) {
            continue;
        }
        if (lseek(gpio, 0, SEEK_SET) < 0 || write(gpio, line, 1) != 1) {
            perror("write GPIO value");
            break;
        }
    }
    fclose(commands);
    close(gpio);
    return EXIT_FAILURE;
}
