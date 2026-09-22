#define _GNU_SOURCE
#include "tat.h"
#include "vterm.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

struct tat_session {
  VTerm *vterm;
  VTermScreen *vterm_screen;

  int master_fd;
  int viewer_fd;

  pid_t child_pid;
  pid_t viewer_pid;

  pthread_t master_reader_thread;
  pthread_mutex_t vterm_lock;

  bool headless;
  bool running;
};

// close without overwriting `errno`
static void tat__close(int fd) {
  int saved_errno = errno;
  close(fd);
  errno = saved_errno;
}

tat_session *tat_session_create(bool headless) {
  tat_session *session = malloc(sizeof(tat_session));

  session->vterm = vterm_new(46, 191);
  if (!session->vterm)
    return NULL;

  vterm_set_utf8(session->vterm, 1);

  session->vterm_screen = vterm_obtain_screen(session->vterm);

  vterm_screen_enable_altscreen(session->vterm_screen, 1);

  vterm_screen_reset(session->vterm_screen, 1);

  session->master_fd = -1;
  session->viewer_fd = -1;
  session->child_pid = -1;

  session->headless = headless;

  session->running = 0;

  return session;
}

// create a fifo file to display a mirror of the program in a different terminal
static int tat__start_viewer() {
  char fifo[128];

  snprintf(fifo, sizeof(fifo), "/tmp/tat-%d.fifo", getpid());

  unlink(fifo);

  if (mkfifo(fifo, 0600) < 0)
    return -1;

  if (fork() == 0) {
    execlp("ghostty", "ghostty", "-e", "cat", fifo, (char *)NULL);
    _exit(127);
  }

  int display_fd = open(fifo, O_WRONLY);
  if (display_fd < 0)
    return -1;

  return display_fd;
}

static void *tat__master_reader(void *arg) {
  tat_session *session = arg;

  char master_buf[4096];

  while (session->running) {
    ssize_t br_master =
        read(session->master_fd, master_buf, sizeof(master_buf));

    if (br_master > 0) {
      size_t offset;

      //
      // write to vterm
      //

      offset = 0;
      pthread_mutex_lock(&session->vterm_lock);
      vterm_input_write(session->vterm, master_buf, br_master);
      pthread_mutex_unlock(&session->vterm_lock);

      //
      // write to viewer
      //

      if (!session->headless) {
        offset = 0;

        while (offset < (size_t)br_master) {
          ssize_t bw_display = write(session->viewer_fd, master_buf + offset,
                                     br_master - offset);

          if (bw_display > 0) {
            offset += bw_display;
            continue;
          }

          if (bw_display < 0 && errno == EINTR)
            continue;

          if (bw_display < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            usleep(10000);
            continue;
          }

          return NULL;
        }
      }

      continue;
    }

    if (br_master < 0) {
      if (errno == EINTR)
        continue;

      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EIO) {
        usleep(10000);
        continue;
      }
    }
  }

  return NULL;
}

int tat_start_program(tat_session *session, const char *program_path,
                      const char *program_name) {
  if (session == NULL || program_path == NULL || program_name == NULL) {
    errno = EINVAL;
    return -1;
  }

  if (session->running) {
    errno = EBUSY;
    return -1;
  }

  int display_fd = -1;
  if (!session->headless) {
    if ((display_fd = tat__start_viewer()) < 0)
      return -1;
  }

  int master_fd = posix_openpt(O_RDWR | O_NOCTTY);

  if (master_fd < 0)
    return -1;

  if (grantpt(master_fd) < 0) {
    tat__close(master_fd);
    return -1;
  }

  if (unlockpt(master_fd) < 0) {
    tat__close(master_fd);
    return -1;
  }

  char *slave_name = ptsname(master_fd);
  if (slave_name == NULL) {
    tat__close(master_fd);
    return -1;
  }

  struct winsize slave_ws = {0};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &slave_ws) < 0)
    _exit(127);

  pid_t child_pid = fork();
  if (child_pid < 0) {
    tat__close(master_fd);
    return -1;
  }

  if (child_pid == 0) {
    tat__close(display_fd);

    if (setsid() < 0)
      _exit(127);

    tat__close(master_fd);

    int slave_fd = open(slave_name, O_RDWR);
    if (slave_fd < 0)
      _exit(127);

    if (ioctl(slave_fd, TIOCSCTTY, 0) < 0)
      _exit(127);

    if (ioctl(slave_fd, TIOCSWINSZ, &slave_ws) < 0)
      _exit(127);

    if (dup2(slave_fd, STDIN_FILENO) < 0 || dup2(slave_fd, STDOUT_FILENO) < 0 ||
        dup2(slave_fd, STDERR_FILENO) < 0) {
      _exit(127);
    }

    if (slave_fd > STDERR_FILENO)
      tat__close(slave_fd);

    execl(program_path, program_name, (char *)NULL);

    _exit(127);
  }

  int flags = fcntl(master_fd, F_GETFL);
  if (flags == -1 || fcntl(master_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    return -1;
  }

  session->master_fd = master_fd;
  session->viewer_fd = display_fd;
  session->child_pid = child_pid;
  session->running = true;

  int err = pthread_create(&session->master_reader_thread, NULL,
                           tat__master_reader, session);
  if (err != 0) {
    errno = err;
    return -1;
  }

  return 0;
}

void tat_session_destroy(tat_session *session) {
  if (session == NULL)
    return;

  session->running = false;

  vterm_free(session->vterm);

  tat__close(session->viewer_fd);
  tat__close(session->master_fd);

  session->viewer_fd = -1;
  session->master_fd = -1;

  if (session->child_pid > 0) {
    kill(session->child_pid, SIGKILL);
    waitpid(session->child_pid, NULL, 0);
  }

  if (session->viewer_pid > 0) {
    kill(session->viewer_pid, SIGKILL);
    waitpid(session->viewer_pid, NULL, 0);
  }

  free(session);
  session = NULL;
}

// Find a string in the terminal. Returns `true` if found within `timeout_ms`
// else `false`
bool tat_expect_string(tat_session *session, const char *s, int timeout_ms) {
  for (;;) {
    int rows, cols;

    pthread_mutex_lock(&session->vterm_lock);

    vterm_get_size(session->vterm, &rows, &cols);

    VTermRect rect = {
        .start_row = 0,
        .end_row = rows,
        .start_col = 0,
        .end_col = cols,
    };

    size_t len = vterm_screen_get_text(session->vterm_screen, NULL, 0, rect);

    char *expected_string_buf = malloc(len + 1);

    if (expected_string_buf == NULL) {
      pthread_mutex_unlock(&session->vterm_lock);
      return false;
    }

    vterm_screen_get_text(session->vterm_screen, expected_string_buf, len,
                          rect);
    expected_string_buf[len] = '\0';

    pthread_mutex_unlock(&session->vterm_lock);

    bool found = strstr(expected_string_buf, s) != NULL;
    free(expected_string_buf);

    if (found)
      return true;

    if (timeout_ms <= 0)
      return false;

    int step_ms = timeout_ms > 100 ? 100 : timeout_ms;

    usleep((useconds_t)step_ms * 1000);
    timeout_ms -= step_ms;
  }
}
int tat_send_key(tat_session *session, tat_key key) {
  if (session == NULL || !session->running || session->master_fd < 0) {
    errno = EINVAL;
    return -1;
  }

  unsigned char c = (unsigned char)key;

  ssize_t bytes_written;

  do {
    bytes_written = write(session->master_fd, &c, 1);
  } while (bytes_written < 0 && errno == EINTR);

  if (bytes_written != 1)
    return -1;

  return 0;
}
