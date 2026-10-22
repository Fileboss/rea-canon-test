/*
 * libbjnp - Canon BJNP network printing client (printer side, port 8611).
 *
 * Protocol as observed in the Windows port monitor (docs/protocole-mx870.md §4.6):
 * UDP 0x10 opens a session, TCP 0x21 carries acknowledged data chunks,
 * UDP 0x11 closes the session. UDP 0x01/0x30 discover and identify printers.
 */

#ifndef BJNP_H
#define BJNP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BJNP_PORT 8611

typedef struct {
  char host[64];          /* numeric address */
  char device_id[1024];   /* IEEE 1284 device ID, may be empty */
} bjnp_printer_t;

typedef struct bjnp_job_s bjnp_job_t;

/* Broadcast discovery on all IPv4 interfaces; returns the number found. */
int bjnp_discover(bjnp_printer_t *printers, int max, int timeout_ms);

/* Read the IEEE 1284 device ID of the printer at host. Returns 0 on success. */
int bjnp_get_device_id(const char *host, char *id, size_t idlen, int timeout_ms);

/* Open a print session; err receives a message on failure. */
bjnp_job_t *bjnp_job_open(const char *host, const char *user, const char *title,
                          char *err, size_t errlen);

/* Send data, blocking until the printer accepted all of it. Returns 0 on success. */
int bjnp_job_write(bjnp_job_t *job, const void *data, size_t len);

/* Close the session and free the job. Returns 0 on success. */
int bjnp_job_close(bjnp_job_t *job);

/* Last error message of a job. */
const char *bjnp_job_error(const bjnp_job_t *job);

#ifdef __cplusplus
}
#endif

#endif /* BJNP_H */
