#include "tat.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
  tat_session *session = tat_session_create("ghostty", "ghostty", false);
  assert(session != NULL);

  assert(tat_start_program(session, "/usr/bin/htop", "htop") >= 0);

  assert(tat_send_key(session, TAT_KEY_T) == 0);

  assert(tat_expect_string(session, "Main", 1000) == true);
  assert(!tat_expect_string(session, "A String That Does Not Exist", 100));

  tat_session_destroy(session);
  return 0;
}
