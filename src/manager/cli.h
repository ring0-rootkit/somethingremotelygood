#ifndef CLI_H
#define CLI_H

int cmd_add_user(int argc, char **argv);
int cmd_add_container(int argc, char **argv);
int cmd_list_anomalies(void);
int cmd_list_reports(void);
int cmd_review_anomaly(int argc, char **argv);
void usage(const char *prog);

#endif
