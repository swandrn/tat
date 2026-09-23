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

typedef enum {
  TAT_KEY_BACKSPACE = 8,
  TAT_KEY_TAB = 9,
  TAT_KEY_ENTER = 10,

  TAT_KEY_SPACE = 32,

  TAT_KEY_0 = '0',
  TAT_KEY_1 = '1',
  TAT_KEY_2 = '2',
  TAT_KEY_3 = '3',
  TAT_KEY_4 = '4',
  TAT_KEY_5 = '5',
  TAT_KEY_6 = '6',
  TAT_KEY_7 = '7',
  TAT_KEY_8 = '8',
  TAT_KEY_9 = '9',

  TAT_KEY_A = 'a',
  TAT_KEY_B = 'b',
  TAT_KEY_C = 'c',
  TAT_KEY_D = 'd',
  TAT_KEY_E = 'e',
  TAT_KEY_F = 'f',
  TAT_KEY_G = 'g',
  TAT_KEY_H = 'h',
  TAT_KEY_I = 'i',
  TAT_KEY_J = 'j',
  TAT_KEY_K = 'k',
  TAT_KEY_L = 'l',
  TAT_KEY_M = 'm',
  TAT_KEY_N = 'n',
  TAT_KEY_O = 'o',
  TAT_KEY_P = 'p',
  TAT_KEY_Q = 'q',
  TAT_KEY_R = 'r',
  TAT_KEY_S = 's',
  TAT_KEY_T = 't',
  TAT_KEY_U = 'u',
  TAT_KEY_V = 'v',
  TAT_KEY_W = 'w',
  TAT_KEY_X = 'x',
  TAT_KEY_Y = 'y',
  TAT_KEY_Z = 'z',
} tat_key;

typedef struct tat_session tat_session;

typedef struct {
  bool headless;

  char viewer_terminal_path[256];
  char viewer_terminal_name[32];

  int rows;
  int cols;
} tat_config;

tat_session *tat_session_create(tat_config *config);

int tat_start_program(tat_session *session, const char *program_path,
                      const char *program_name);

void tat_session_destroy(tat_session *session);

bool tat_expect_string(tat_session *session, const char *s, int timeout_ms);

int tat_send_key(tat_session *session, tat_key key);
