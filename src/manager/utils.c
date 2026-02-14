#include "utils.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int write_temp(const char *template_str, const void *data, size_t len, char *out_path, size_t out_len) {
    char path[256];
    snprintf(path, sizeof(path), "%s.XXXXXX", template_str);
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    ssize_t w = write(fd, data, len);
    if ((size_t)w != len) { close(fd); unlink(path); return -1; }
    close(fd);
    chmod(path, S_IRUSR | S_IWUSR);
    if (out_path && out_len) strncpy(out_path, path, out_len);
    return 0;
}

int run_cmdv(char *const argv[]) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    } else if (pid < 0) {
        return -1;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

int run_cmd_shell(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    } else if (pid < 0) return -1;
    int status = 0; waitpid(pid, &status, 0); return status;
}
