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

  assert(tat_start_program(session, "/usr/bin/htop", "htop") == 0);

  assert(tat_send_key(NULL, TAT_KEY_T) < 0);

  tat_session_destroy(session);
  return 0;
}
