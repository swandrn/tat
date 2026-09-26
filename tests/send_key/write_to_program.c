#include "tat.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
  tat_config config = {
      .headless = false,
      .viewer_terminal_path = "/usr/bin/ghostty",
      .viewer_terminal_name = "ghostty",
      .cols = 100,
      .rows = 40,
  };

  tat_session *session = tat_session_create(&config);
  assert(session != NULL);

  assert(tat_start_program(session, "/usr/bin/vi", "vi") == 0);

  assert(tat_expect_string(session, "VIM", 1000));

  assert(tat_send_key(session, TAT_KEY_A) == 0);

  assert(tat_send_key(session, TAT_KEY_UPPER_H) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_E) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_L) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_L) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_O) == 0);
  assert(tat_send_key(session, TAT_KEY_SPACE) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_F) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_R) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_O) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_M) == 0);
  assert(tat_send_key(session, TAT_KEY_SPACE) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_T) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_A) == 0);
  assert(tat_send_key(session, TAT_KEY_UPPER_T) == 0);

  assert(tat_expect_string(session, "HELLO FROM TAT", 100));

  tat_session_destroy(session);

  return 0;
}
