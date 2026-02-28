#ifdef __amigaos4__
#define __USE_INLINE__
#endif
#include <dos/dostags.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>
#include <sys/time.h>

// on Amiga UADE IPC is done via PIPE:
// NOTE: PIPE: does not work together with C (f)read/(f)write, at least on AROS,
// so dos.library is used directly instead

ssize_t uade_atomic_read(int fd, const void *buf, size_t count)
{
  char *b = (char *) buf;
  ssize_t bytes_read = 0;
  ssize_t ret;
  while (bytes_read < count) {
    ret = Read(fd, &b[bytes_read], count - bytes_read);
    if (ret < 0) {
      return -1;
    } else if (ret == 0) {
      return 0;
    }
    bytes_read += ret;
  }
  return bytes_read;
}

ssize_t uade_atomic_write(int fd, const void *buf, size_t count)
{
  char *b = (char *) buf;
  ssize_t bytes_written = 0;
  ssize_t ret;
  while (bytes_written < count) {
    ret = Write(fd, &b[bytes_written], count - bytes_written);
    if (ret < 0) {
      return -1;
    }
    bytes_written += ret;
  }
  return bytes_written;
}

void uade_arch_kill_and_wait_uadecore(struct uade_ipc *ipc, pid_t *uadepid)
{
    if (*uadepid == 0)
        return;

    if (uade_send_short_message(UADE_COMMAND_QUIT, ipc)) {
        uade_warning("Could not send poison pill to uadecore (%d)\n", *uadepid);
    }

    Close(ipc->in_fd);
    Close(ipc->out_fd);

    *uadepid = 0;
}

int uade_arch_spawn(struct uade_ipc *ipc, pid_t *uadepid, const char *uadename, const int *keep_fds)
{
    (void)keep_fds; // unused
    char in_pipe[32];
    char out_pipe[32];
    char command[1024];

    struct timeval tv;
    gettimeofday(&tv, NULL);
    int pipe_id = (getpid() & 0xFFF00000) | (tv.tv_sec & 0x0000FFFF) | (tv.tv_usec & 0x000F0000);

    snprintf(in_pipe, sizeof in_pipe, "PIPE:uadecore-%d", pipe_id);
    snprintf(out_pipe, sizeof out_pipe, "PIPE:uade-%d", pipe_id);

    BPTR out_fd = Open(out_pipe, MODE_NEWFILE);
    if (!out_fd) {
        uade_warning("Could not open write pipe %s (%d)\n", out_pipe, IoErr());
        return -1;
    }
    // create pipe file
    if (uade_atomic_write(out_fd, out_pipe, 1) != 1) {
        uade_warning("Could not write to pipe %s (%d)\n", out_pipe, IoErr());
        Close(out_fd);
        return -1;
    }

    snprintf(command, sizeof command, "\"%s\" -i %d -o %d", uadename, pipe_id, pipe_id);

    // TODO stderr does not work on AROS
    LONG res = SystemTags((STRPTR)command,
        SYS_Asynch,    TRUE,
        SYS_Input,     0,
        SYS_Output,    0,
        NP_Error,      0,
        TAG_END
    );

    if (res < 0) {
        uade_warning("SystemTags failed for: %s (%d)\n", uadename, res);
        Close(out_fd);
        return -1;
    }

    // wait for read pipe to be created
    int count = 0;
    BPTR in_fd;
    do {
        in_fd = Open(in_pipe, MODE_OLDFILE);
        if (!in_fd)
           Delay(20);
    } while (!in_fd && count++ < 50 * 5); // wait max 5 seconds

    if (!in_fd) {
        uade_warning("Could not open read pipe %s (%d)\n", in_pipe, IoErr());
        Close(out_fd);
        return -1;
    }
    // drain dummy data
    if (uade_atomic_read(in_fd, in_pipe, 1) != 1) {
        uade_warning("Could not read from pipe %s (%d)\n", in_pipe, IoErr());
        Close(out_fd);
        Close(in_fd);
        return -1;
    }

    *uadepid = pipe_id;
    uade_set_peer(ipc, 1, in_fd, out_fd);
    return 0;
}
