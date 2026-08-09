#ifndef APPLICATIONS_CMD_TEST_HPP
#define APPLICATIONS_CMD_TEST_HPP

#ifdef __cplusplus
extern "C" {
#endif

int psram_test(int argc, char **argv);
int wlh_status(int argc, char **argv);
int wlh_reset(int argc, char **argv);
int iperf(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATIONS_CMD_TEST_HPP */
