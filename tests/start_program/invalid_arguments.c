#include "tat.h"
#include <assert.h>
#include <errno.h>
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

  assert(tat_start_program(NULL, "/usr/bin/htop", "htop") < 0);
  assert(errno == EINVAL);
  errno = 0;

  assert(tat_start_program(session, NULL, "htop") < 0);
  assert(errno == EINVAL);
  errno = 0;

  assert(tat_start_program(session, "/usr/bin/htop", NULL) < 0);
  assert(errno == EINVAL);

  tat_session_destroy(session);

  return 0;
}
