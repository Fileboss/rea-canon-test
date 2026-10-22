/*
 * mx870-bjnp - BJNP utility for the Canon MX870.
 *
 *   mx870-bjnp discover                     list BJNP printers on the LAN
 *   mx870-bjnp id HOST                      print the IEEE 1284 device ID
 *   mx870-bjnp send HOST FILE [TITLE]       send a prepared stream (FILE "-" = stdin)
 *   mx870-bjnp relay HOST [LISTEN_PORT]     forward raw TCP jobs (127.0.0.1:9870) to HOST
 *
 * The relay lets a CUPS queue print through BJNP with a plain socket:// URI:
 * third-party CUPS backends cannot be installed on the sealed macOS system volume.
 * HOST may be "address" or "address:port".
 */

#include "../libbjnp/bjnp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *user_name(void)
{
  struct passwd *pw = getpwuid(getuid());

  return pw ? pw->pw_name : "mx870";
}

/* Stream fd to a new BJNP job. Returns 0 on success. */
static int forward(const char *host, int fd, const char *title)
{
  char err[256], buf[65536];
  bjnp_job_t *job = bjnp_job_open(host, user_name(), title, err, sizeof(err));
  ssize_t n;
  size_t total = 0;
  int rc = 0;

  if (!job) {
    fprintf(stderr, "mx870-bjnp: %s\n", err);
    return -1;
  }
  while ((n = read(fd, buf, sizeof(buf))) > 0 || (n < 0 && errno == EINTR)) {
    if (n < 0)
      continue;
    if (bjnp_job_write(job, buf, (size_t)n)) {
      fprintf(stderr, "mx870-bjnp: %s\n", bjnp_job_error(job));
      rc = -1;
      break;
    }
    total += (size_t)n;
  }
  if (n < 0)
    rc = -1;
  if (bjnp_job_close(job))
    fprintf(stderr, "mx870-bjnp: warning: close not acknowledged\n");
  fprintf(stderr, "mx870-bjnp: %s %zu bytes to %s\n", rc ? "aborted after" : "sent", total, host);
  return rc;
}

static int relay(const char *host, int port)
{
  struct sockaddr_in addr = {0};
  int srv = socket(AF_INET, SOCK_STREAM, 0), on = 1;

  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (srv < 0 || bind(srv, (struct sockaddr *)&addr, sizeof(addr)) || listen(srv, 4)) {
    perror("mx870-bjnp: relay");
    return 1;
  }
  signal(SIGPIPE, SIG_IGN);
  fprintf(stderr, "mx870-bjnp: relaying 127.0.0.1:%d -> %s (BJNP)\n", port, host);
  for (;;) {
    int c = accept(srv, NULL, NULL);

    if (c < 0) {
      if (errno == EINTR)
        continue;
      perror("mx870-bjnp: accept");
      return 1;
    }
    /* One job at a time: the printer only takes one session anyway. */
    forward(host, c, "CUPS job");
    close(c);
  }
}

int main(int argc, char **argv)
{
  if (argc >= 2 && !strcmp(argv[1], "discover")) {
    bjnp_printer_t p[16];
    int n = bjnp_discover(p, 16, 2000);

    for (int i = 0; i < n; i++)
      printf("%s\t%s\n", p[i].host, p[i].device_id);
    if (n <= 0)
      fprintf(stderr, "mx870-bjnp: no BJNP printer answered\n");
    return n > 0 ? 0 : 1;
  }
  if (argc == 3 && !strcmp(argv[1], "id")) {
    char id[1024];

    if (bjnp_get_device_id(argv[2], id, sizeof(id), 2000)) {
      fprintf(stderr, "mx870-bjnp: no answer from %s\n", argv[2]);
      return 1;
    }
    puts(id);
    return 0;
  }
  if ((argc == 4 || argc == 5) && !strcmp(argv[1], "send")) {
    int fd = strcmp(argv[3], "-") ? open(argv[3], O_RDONLY) : 0;

    if (fd < 0) {
      perror(argv[3]);
      return 1;
    }
    return forward(argv[2], fd, argc == 5 ? argv[4] : argv[3]) ? 1 : 0;
  }
  if ((argc == 3 || argc == 4) && !strcmp(argv[1], "relay"))
    return relay(argv[2], argc == 4 ? atoi(argv[3]) : 9870);

  fprintf(stderr,
          "usage: mx870-bjnp discover\n"
          "       mx870-bjnp id HOST\n"
          "       mx870-bjnp send HOST FILE|- [TITLE]\n"
          "       mx870-bjnp relay HOST [LISTEN_PORT]\n");
  return 2;
}
