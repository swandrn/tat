#pragma once
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>

#ifdef TAT_ENABLE_DEBUG
#define TAT_ERROR(...)                                                         \
  do {                                                                         \
    fprintf(stderr, __VA_ARGS__);                                              \
    fputc('\n', stderr);                                                       \
  } while (0)
#else
#define TAT_ERROR(...) ((void)0)
#endif

#define TAT_KEY_BACKSPACE 127
#define TAT_KEY_TAB '\t'
#define TAT_KEY_ENTER '\r'
#define TAT_KEY_SPACE 32

typedef struct tat_session tat_session;

typedef struct {
  bool headless;

  char viewer_terminal_path[256];
  char viewer_terminal_name[32];

  int rows;
  int cols;
} tat_config;

tat_session *tat_session_create(tat_config *config);

void tat_session_destroy(tat_session *session);

// Starts the program provided by `program_path` and `program_name`. Returns 0
// on success and -1 on error with errno set to explain the error.
// `program_name` (and optionally `viewer_terminal_name`) is executed with
// execl, so `program_path` (and optionally `viewer_terminal_path`) must be the
// full path, whether absolute or relative.
//
// The program is started by the time this function returns, however it might
// not be rendering anything. It is recommended to use `tat_expect_string` with
// a suitable timeout to ensure the
// program is ready for use.
int tat_start_program(tat_session *session, const char *program_path,
                      const char *program_name);

int tat_send_key(tat_session *session, unsigned char key);

// Find a string in the terminal. Returns true if found within `timeout_ms`
// else false
bool tat_expect_string(tat_session *session, const char *s, int timeout_ms);
