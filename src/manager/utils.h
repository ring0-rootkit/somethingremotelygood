#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

int write_temp(const char *template_str, const void *data, size_t len, char *out_path, size_t out_len);
int run_cmdv(char *const argv[]);
int run_cmd_shell(const char *cmd);

#endif
