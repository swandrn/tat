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

#ifdef __cplusplus
#include <atomic>
using namespace std;
#define _Atomic(T) atomic<T>
#else
#include <stdatomic.h>
#endif

struct tat_session {
  VTerm *vterm;
  VTermScreen *vterm_screen;

  int rows;
  int cols;

  int master_fd;
  int viewer_fd;

  pid_t child_pid;
  pid_t viewer_pid;

  char viewer_terminal_path[256];
  char viewer_terminal_name[32];

  pthread_t master_reader_thread;
  pthread_mutex_t master_write_lock;
  pthread_mutex_t vterm_lock;

  bool headless;
  _Atomic(bool) running;
};

// Close without overwriting `errno`
static void tat__close(int fd) {
  int saved_errno = errno;
  close(fd);
  errno = saved_errno;
}

// Thread safe write to master fd
static int tat__write_master(tat_session *session, const char *buf,
                             size_t len) {
  pthread_mutex_lock(&session->master_write_lock);

  size_t offset = 0;

  while (offset < len) {
    ssize_t n = write(session->master_fd, buf + offset, len - offset);

    if (n > 0) {
      offset += (size_t)n;
      continue;
    }

    if (n < 0 && errno == EINTR)
      continue;

    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {

      struct pollfd pfd = {
          .fd = session->master_fd,
          .events = POLLOUT,
      };

      int r;
      do {
        r = poll(&pfd, 1, 1000);
      } while (r < 0 && errno == EINTR);

      if (r > 0)
        continue;
    }

    pthread_mutex_unlock(&session->master_write_lock);
    return -1;
  }

  pthread_mutex_unlock(&session->master_write_lock);
  return 0;
}

static void tat__vterm_output(const char *s, size_t len, void *user) {
  tat_session *session = user;

  if (session->master_fd < 0)
    return;

  tat__write_master(session, s, len);
}

static void *tat__master_reader(void *arg) {
  tat_session *session = arg;

  char master_buf[4096];

  while (atomic_load(&session->running)) {
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

// create a fifo file to display a mirror of the program in a different terminal
static int tat__start_viewer(tat_session *session) {
  char fifo[128];

  snprintf(fifo, sizeof(fifo), "/tmp/tat-%d.fifo", getpid());

  unlink(fifo);

  if (mkfifo(fifo, 0600) < 0)
    return -1;

  int err_pipe[2];

  if (pipe2(err_pipe, O_CLOEXEC) < 0)
    return -1;

  pid_t viewer_pid = fork();

  if (viewer_pid < 0)
    return -1;

  if (viewer_pid == 0) {
    tat__close(err_pipe[0]);

    int devnull = open("/dev/null", O_RDWR);

    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);

      if (devnull > STDERR_FILENO)
        tat__close(devnull);
    }

    execl(session->viewer_terminal_path, session->viewer_terminal_name, "-e",
          "cat", fifo, (char *)NULL);

    int err = errno;
    write(err_pipe[1], &err, sizeof(err));
    _exit(127);
  }

  tat__close(err_pipe[1]);

  int viewer_errno;
  ssize_t br_err_pipe;
  do {
    br_err_pipe = read(err_pipe[0], &viewer_errno, sizeof(viewer_errno));
  } while (br_err_pipe < 0 && errno == EINTR);

  tat__close(err_pipe[0]);

  if (br_err_pipe == sizeof(viewer_errno)) {
    errno = viewer_errno;
    return -1;
  }

  session->viewer_pid = viewer_pid;

  int display_fd = open(fifo, O_WRONLY);
  if (display_fd < 0)
    return -1;

  return display_fd;
}

tat_session *tat_session_create(tat_config *config) {
  tat_config default_config = {
      .headless = true,
      .viewer_terminal_path = "",
      .viewer_terminal_name = "",
      .cols = 100,
      .rows = 40,
  };

  if (!config)
    config = &default_config;

  if (!config->headless && (config->viewer_terminal_path[0] == '\0' ||
                            config->viewer_terminal_name[0] == '\0'))
    return NULL;

  tat_session *session = calloc(1, sizeof(tat_session));
  if (!session)
    return NULL;

  int written = snprintf(session->viewer_terminal_path,
                         sizeof(session->viewer_terminal_path), "%s",
                         config->viewer_terminal_path);
  if (written < 0 || (size_t)written >= sizeof(session->viewer_terminal_path)) {
    TAT_ERROR("viewer_terminal_path was likely truncated");
    free(session);
    return NULL;
  }

  written = snprintf(session->viewer_terminal_name,
                     sizeof(session->viewer_terminal_name), "%s",
                     config->viewer_terminal_name);
  if (written < 0 || (size_t)written >= sizeof(session->viewer_terminal_name)) {
    TAT_ERROR("viewer_terminal_name was likely truncated");
    free(session);
    return NULL;
  }

  session->rows = config->rows;
  session->cols = config->cols;

  session->vterm = vterm_new(session->rows, session->cols);
  if (!session->vterm) {
    free(session);
    return NULL;
  }

  vterm_set_utf8(session->vterm, 1);

  vterm_output_set_callback(session->vterm, tat__vterm_output, session);

  session->vterm_screen = vterm_obtain_screen(session->vterm);

  vterm_screen_enable_altscreen(session->vterm_screen, 1);

  vterm_screen_reset(session->vterm_screen, 1);

  session->master_fd = -1;
  session->viewer_fd = -1;
  session->child_pid = -1;

  pthread_mutex_init(&session->vterm_lock, NULL);

  session->headless = config->headless;

  atomic_init(&session->running, false);

  return session;
}

void tat_session_destroy(tat_session *session) {
  if (session == NULL)
    return;

  atomic_store(&session->running, false);

  pthread_join(session->master_reader_thread, NULL);

  if (session->child_pid > 0) {
    kill(session->child_pid, SIGKILL);
    waitpid(session->child_pid, NULL, 0);
  }

  if (session->viewer_pid > 0) {
    kill(session->viewer_pid, SIGKILL);
    waitpid(session->viewer_pid, NULL, 0);
  }

  tat__close(session->viewer_fd);
  tat__close(session->master_fd);

  vterm_free(session->vterm);

  free(session);
}

// TODO: Support passing args or flags to program
int tat_start_program(tat_session *session, const char *program_path,
                      const char *program_name) {
  if (session == NULL || program_path == NULL || program_name == NULL) {
    errno = EINVAL;
    return -1;
  }

  if (atomic_load(&session->running)) {

    errno = EBUSY;
    return -1;
  }

  int display_fd = -1;
  if (!session->headless) {
    if ((display_fd = tat__start_viewer(session)) < 0)
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

  struct winsize slave_ws = {
      .ws_row = session->rows,
      .ws_col = session->cols,
  };

  int err_pipe[2];

  if (pipe2(err_pipe, O_CLOEXEC) < 0)
    return -1;

  pid_t program_pid = fork();
  if (program_pid < 0) {
    tat__close(master_fd);
    return -1;
  }

  if (program_pid == 0) {
    tat__close(display_fd);

    int err;

    if (setsid() < 0) {
      err = errno;
      write(err_pipe[1], &err, sizeof(err));
      _exit(127);
    }

    tat__close(master_fd);

    int slave_fd = open(slave_name, O_RDWR);
    if (slave_fd < 0) {
      err = errno;
      write(err_pipe[1], &err, sizeof(err));
      _exit(127);
    }

    if (ioctl(slave_fd, TIOCSCTTY, 0) < 0) {
      err = errno;
      write(err_pipe[1], &err, sizeof(err));
      _exit(127);
    }

    if (ioctl(slave_fd, TIOCSWINSZ, &slave_ws) < 0) {
      err = errno;
      write(err_pipe[1], &err, sizeof(err));
      _exit(127);
    }

    if (dup2(slave_fd, STDIN_FILENO) < 0 || dup2(slave_fd, STDOUT_FILENO) < 0 ||
        dup2(slave_fd, STDERR_FILENO) < 0) {
      err = errno;
      write(err_pipe[1], &err, sizeof(err));
      _exit(127);
    }

    if (slave_fd > STDERR_FILENO)
      tat__close(slave_fd);

    execl(program_path, program_name, (char *)NULL);

    err = errno;
    write(err_pipe[1], &err, sizeof(err));
    _exit(127);
  }

  tat__close(err_pipe[1]);

  int program_errno;
  ssize_t br_err_pipe;
  do {
    br_err_pipe = read(err_pipe[0], &program_errno, sizeof(program_errno));
  } while (br_err_pipe < 0 && errno == EINTR);

  tat__close(err_pipe[0]);

  if (br_err_pipe == sizeof(program_errno)) {
    errno = program_errno;
    return -1;
  }

  int flags = fcntl(master_fd, F_GETFL);
  if (flags == -1 || fcntl(master_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    return -1;
  }

  session->master_fd = master_fd;
  session->viewer_fd = display_fd;
  session->child_pid = program_pid;

  atomic_store(&session->running, true);

  int err = pthread_create(&session->master_reader_thread, NULL,
                           tat__master_reader, session);
  if (err != 0) {
    atomic_store(&session->running, false);
    errno = err;
    return -1;
  }

  return 0;
}

int tat_send_key(tat_session *session, tat_key key) {
  if (session == NULL || !atomic_load(&session->running) ||
      session->master_fd < 0) {
    errno = EINVAL;
    return -1;
  }

  if (key < 0 || key > 127) {
    errno = EINVAL;
    return -1;
  }

  unsigned char c = (unsigned char)key;

  return tat__write_master(session, (const char *)&c, 1);
}

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

    size_t preparsed_screen_size =
        vterm_screen_get_text(session->vterm_screen, NULL, 0, rect);

    char *expected_string_buf = malloc(preparsed_screen_size + 1);

    if (expected_string_buf == NULL) {
      pthread_mutex_unlock(&session->vterm_lock);
      return false;
    }

    vterm_screen_get_text(session->vterm_screen, expected_string_buf,
                          preparsed_screen_size, rect);
    expected_string_buf[preparsed_screen_size] = '\0';

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
