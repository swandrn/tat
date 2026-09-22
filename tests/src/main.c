#include "tat.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
  tat_session *session = tat_session_create(false);
  if (!session) {
    exit(1);
  }

  if (tat_start_program(session, "/usr/bin/htop", "htop") < 0) {
    perror("tat_start_program");
    return 1;
  }
  tat_send_key(session, TAT_KEY_T); // display tree view

  if (tat_expect_string(session, "Main", 1000)) {
    puts("Main found");
  } else {
    puts("No luck");
  }

  tat_session_destroy(session);
  return 0;
}
