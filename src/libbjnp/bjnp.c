/*
 * libbjnp implementation (POSIX sockets).
 */

#include "bjnp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DEV_PRINTER 0x01
#define RESPONSE 0x80

#define CMD_DISCOVER 0x01
#define CMD_JOB_DETAILS 0x10
#define CMD_CLOSE 0x11
#define CMD_SEND 0x21
#define CMD_GET_ID 0x30

#define HDR_LEN 16
#define CHUNK 16384           /* data bytes per CMD_SEND */
#define BUSY_RETRY_MS 500     /* Windows retries a zero acknowledgement every 500 ms */
#define BUSY_TIMEOUT_MS 10000 /* ... and gives up after 10 s */
#define UDP_TRIES 3

struct bjnp_job_s {
  struct sockaddr_storage addr;
  socklen_t addrlen;
  int tcp;
  uint16_t session;
  char error[256];
};

static uint16_t next_seq(void)
{
  static uint16_t seq;

  return ++seq;
}

static void put16(uint8_t *p, unsigned v)
{
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
  put16(p, v >> 16);
  put16(p + 2, v & 0xffff);
}

static unsigned get16(const uint8_t *p)
{
  return (unsigned)p[0] << 8 | p[1];
}

static uint32_t get32(const uint8_t *p)
{
  return (uint32_t)get16(p) << 16 | get16(p + 2);
}

static void header(uint8_t *h, int cmd, uint16_t seq, uint16_t session, uint32_t len)
{
  memcpy(h, "BJNP", 4);
  h[4] = DEV_PRINTER;
  h[5] = (uint8_t)cmd;
  put16(h + 6, 0);
  put16(h + 8, seq);
  put16(h + 10, session);
  put32(h + 12, len);
}

/* Validate a response header; returns payload length or -1. */
static long check_response(const uint8_t *h, size_t got, int cmd)
{
  if (got < HDR_LEN || memcmp(h, "BJNP", 4) || h[4] != (DEV_PRINTER | RESPONSE) || h[5] != cmd ||
      get16(h + 6) != 0)
    return -1;
  return (long)get32(h + 12);
}

/* host is "name", "a.b.c.d" or "name:port" (port defaults to 8611). */
static int resolve(const char *host, struct sockaddr_storage *addr, socklen_t *len, int socktype)
{
  struct addrinfo hints = {0}, *res;
  char name[256], port[8];
  const char *colon = strrchr(host, ':');

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socktype;
  if (colon && !strchr(colon + 1, ']') && strchr(host, ':') == colon) {
    snprintf(name, sizeof(name), "%.*s", (int)(colon - host), host);
    snprintf(port, sizeof(port), "%s", colon + 1);
  } else {
    snprintf(name, sizeof(name), "%s", host);
    snprintf(port, sizeof(port), "%d", BJNP_PORT);
  }
  if (getaddrinfo(name, port, &hints, &res))
    return -1;
  memcpy(addr, res->ai_addr, res->ai_addrlen);
  *len = res->ai_addrlen;
  freeaddrinfo(res);
  return 0;
}

static int wait_readable(int fd, int timeout_ms)
{
  struct pollfd p = {fd, POLLIN, 0};

  return poll(&p, 1, timeout_ms) == 1 ? 0 : -1;
}

/* One UDP request/response exchange with retries. Returns response size or -1. */
static long udp_exchange(const struct sockaddr_storage *addr, socklen_t addrlen, int cmd,
                         uint16_t session, const void *payload, size_t plen,
                         uint8_t *resp, size_t resplen, int timeout_ms)
{
  uint8_t req[HDR_LEN + 512];
  int fd = socket(addr->ss_family, SOCK_DGRAM, 0);
  long got = -1;

  if (fd < 0 || plen > sizeof(req) - HDR_LEN) {
    if (fd >= 0)
      close(fd);
    return -1;
  }
  for (int t = 0; t < UDP_TRIES && got < 0; t++) {
    uint16_t seq = next_seq();

    header(req, cmd, seq, session, (uint32_t)plen);
    if (plen)
      memcpy(req + HDR_LEN, payload, plen);
    if (sendto(fd, req, HDR_LEN + plen, 0, (const struct sockaddr *)addr, addrlen) < 0)
      break;
    while (wait_readable(fd, timeout_ms) == 0) {
      ssize_t n = recv(fd, resp, resplen, 0);

      /* Ignore stale answers to earlier tries. */
      if (n >= HDR_LEN && check_response(resp, (size_t)n, cmd) >= 0 && get16(resp + 8) == seq) {
        got = n;
        break;
      }
    }
  }
  close(fd);
  return got;
}

int bjnp_get_device_id(const char *host, char *id, size_t idlen, int timeout_ms)
{
  struct sockaddr_storage addr;
  socklen_t addrlen;
  uint8_t resp[HDR_LEN + 1040];
  long n;
  unsigned len;

  if (resolve(host, &addr, &addrlen, SOCK_DGRAM))
    return -1;
  n = udp_exchange(&addr, addrlen, CMD_GET_ID, 0, NULL, 0, resp, sizeof(resp), timeout_ms);
  if (n < HDR_LEN + 2)
    return -1;
  /* Payload: u16 total length (including itself), then the ID string. */
  len = get16(resp + HDR_LEN);
  if (len < 2 || HDR_LEN + len > (size_t)n)
    return -1;
  len -= 2;
  if (len >= idlen)
    len = (unsigned)idlen - 1;
  memcpy(id, resp + HDR_LEN + 2, len);
  id[len] = '\0';
  return 0;
}

int bjnp_discover(bjnp_printer_t *printers, int max, int timeout_ms)
{
  struct ifaddrs *ifs, *ifa;
  uint8_t req[HDR_LEN], resp[256];
  int fd, on = 1, found = 0;
  struct timespec start, now;

  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    return -1;
  setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
  header(req, CMD_DISCOVER, next_seq(), 0, 0);

  if (getifaddrs(&ifs) == 0) {
    for (ifa = ifs; ifa; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET && ifa->ifa_broadaddr &&
          (ifa->ifa_flags & IFF_BROADCAST) && !(ifa->ifa_flags & IFF_LOOPBACK)) {
        struct sockaddr_in b = *(struct sockaddr_in *)ifa->ifa_broadaddr;

        b.sin_port = htons(BJNP_PORT);
        sendto(fd, req, sizeof(req), 0, (struct sockaddr *)&b, sizeof(b));
      }
    }
    freeifaddrs(ifs);
  }

  clock_gettime(CLOCK_MONOTONIC, &start);
  for (;;) {
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    long left;
    ssize_t n;
    char host[64];
    int dup = 0;

    clock_gettime(CLOCK_MONOTONIC, &now);
    left = timeout_ms - ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
    if (left <= 0 || found >= max || wait_readable(fd, (int)left))
      break;
    n = recvfrom(fd, resp, sizeof(resp), 0, (struct sockaddr *)&from, &fromlen);
    if (n < HDR_LEN || check_response(resp, (size_t)n, CMD_DISCOVER) < 0)
      continue;
    inet_ntop(AF_INET, &from.sin_addr, host, sizeof(host));
    for (int i = 0; i < found; i++)
      dup |= !strcmp(printers[i].host, host);
    if (dup)
      continue;
    snprintf(printers[found].host, sizeof(printers[found].host), "%s", host);
    printers[found].device_id[0] = '\0';
    found++;
  }
  close(fd);

  for (int i = 0; i < found; i++)
    bjnp_get_device_id(printers[i].host, printers[i].device_id, sizeof(printers[i].device_id), 1000);
  return found;
}

static void set_error(bjnp_job_t *job, const char *fmt, ...)
{
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(job->error, sizeof(job->error), fmt, ap);
  va_end(ap);
}

/*
 * Copy a UTF-8 string as UTF-16LE (as the Windows port monitor does). Only
 * ASCII is kept; each other character becomes one '?'.
 */
static void put_utf16(uint8_t *dst, size_t size, const char *s)
{
  for (size_t i = 0; s && *s && i + 2 <= size - 2; s++) {
    uint8_t c = (uint8_t)*s;

    if ((c & 0xc0) == 0x80) /* UTF-8 continuation byte */
      continue;
    dst[i] = c & 0x80 ? '?' : c;
    dst[i + 1] = 0;
    i += 2;
  }
}

bjnp_job_t *bjnp_job_open(const char *host, const char *user, const char *title,
                          char *err, size_t errlen)
{
  bjnp_job_t *job = calloc(1, sizeof(*job));
  uint8_t details[392] = {0}, resp[HDR_LEN + 64];
  char hostname[64] = "";
  long n;
  int one = 1;

  if (!job) {
    snprintf(err, errlen, "out of memory");
    return NULL;
  }
  job->tcp = -1;
  if (resolve(host, &job->addr, &job->addrlen, SOCK_DGRAM)) {
    snprintf(err, errlen, "cannot resolve %s", host);
    free(job);
    return NULL;
  }

  /* 4 + 4 bytes (zero), host name (64), user (64), title (256), UTF-16. */
  gethostname(hostname, sizeof(hostname));
  put_utf16(details + 8, 64, hostname);
  put_utf16(details + 72, 64, user);
  put_utf16(details + 136, 256, title);
  n = udp_exchange(&job->addr, job->addrlen, CMD_JOB_DETAILS, 0, details, sizeof(details),
                   resp, sizeof(resp), 3000);
  if (n < HDR_LEN) {
    snprintf(err, errlen, "%s did not answer the BJNP job request (UDP %d)", host, BJNP_PORT);
    free(job);
    return NULL;
  }
  job->session = (uint16_t)get16(resp + 10);

  if ((job->tcp = socket(job->addr.ss_family, SOCK_STREAM, 0)) < 0 ||
      connect(job->tcp, (struct sockaddr *)&job->addr, job->addrlen)) {
    snprintf(err, errlen, "cannot connect to %s:%d: %s", host, BJNP_PORT, strerror(errno));
    bjnp_job_close(job);
    return NULL;
  }
  setsockopt(job->tcp, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  return job;
}

static int write_all(int fd, const void *buf, size_t len)
{
  const uint8_t *p = buf;

  while (len) {
    ssize_t n = send(fd, p, len, 0);

    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return -1;
    p += n;
    len -= (size_t)n;
  }
  return 0;
}

static int read_all(int fd, void *buf, size_t len, int timeout_ms)
{
  uint8_t *p = buf;

  while (len) {
    ssize_t n;

    if (wait_readable(fd, timeout_ms))
      return -1;
    n = recv(fd, p, len, 0);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return -1;
    p += n;
    len -= (size_t)n;
  }
  return 0;
}

int bjnp_job_write(bjnp_job_t *job, const void *data, size_t len)
{
  const uint8_t *p = data;
  int busy_ms = 0;

  while (len) {
    size_t chunk = len < CHUNK ? len : CHUNK;
    uint8_t hdr[HDR_LEN], ack[HDR_LEN + 4];
    long plen;
    uint32_t accepted;

    header(hdr, CMD_SEND, next_seq(), job->session, (uint32_t)chunk);
    if (write_all(job->tcp, hdr, sizeof(hdr)) || write_all(job->tcp, p, chunk)) {
      set_error(job, "send failed: %s", strerror(errno));
      return -1;
    }
    if (read_all(job->tcp, ack, HDR_LEN, 30000) ||
        (plen = check_response(ack, HDR_LEN, CMD_SEND)) < 4 ||
        read_all(job->tcp, ack + HDR_LEN, 4, 30000)) {
      set_error(job, "no valid acknowledgement from printer");
      return -1;
    }
    for (long skip = plen - 4; skip > 0; skip--) /* tolerate longer acknowledgements */
      if (read_all(job->tcp, ack, 1, 30000))
        return -1;

    accepted = get32(ack + HDR_LEN);
    if (accepted > chunk)
      accepted = (uint32_t)chunk;
    if (!accepted) {
      /* Printer busy: retry the same data, as the Windows port monitor does. */
      if ((busy_ms += BUSY_RETRY_MS) > BUSY_TIMEOUT_MS) {
        set_error(job, "printer busy for more than %d s", BUSY_TIMEOUT_MS / 1000);
        return -1;
      }
      usleep(BUSY_RETRY_MS * 1000);
      continue;
    }
    busy_ms = 0;
    p += accepted;
    len -= accepted;
  }
  return 0;
}

int bjnp_job_close(bjnp_job_t *job)
{
  uint8_t resp[HDR_LEN + 64];
  int rc = 0;

  if (!job)
    return -1;
  if (job->tcp >= 0) {
    if (udp_exchange(&job->addr, job->addrlen, CMD_CLOSE, job->session, NULL, 0, resp,
                     sizeof(resp), 3000) < 0)
      rc = -1;
    close(job->tcp);
  }
  free(job);
  return rc;
}

const char *bjnp_job_error(const bjnp_job_t *job)
{
  return job->error;
}
