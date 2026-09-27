//
// Starting two programs can be useful if two TUIs need to talk to each other,
// or one depends on the other
//
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

  tat_session *first_session = tat_session_create(&config);
  assert(first_session != NULL);

  assert(tat_start_program(first_session, "/usr/bin/top", "top") == 0);

  assert(tat_expect_string(first_session, "PID", 1000));

  tat_session *second_session = tat_session_create(&config);
  assert(second_session != NULL);

  assert(tat_start_program(second_session, "/usr/bin/vi", "vi") == 0);

  assert(tat_expect_string(second_session, "VIM", 1000));

  // First session is still accessible
  assert(tat_expect_string(first_session, "USER", 1000));

  tat_session_destroy(first_session);

  tat_session_destroy(second_session);

  return 0;
}
