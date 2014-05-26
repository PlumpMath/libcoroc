#ifndef _TSC_SERVICE_H_
#define _TSC_SERVICE_H_

#include "../support.h"
#include "../channel.h"
#include "../coroutine.h"

#define TSC_SERVICE_INFINITY (uint64_t)(-1)

enum { TSC_SERVICE_LOCAL = 1, TSC_SERVICE_REMOTE_TCP = 2, };

struct tsc_service;
struct tsc_message;

/* the service callback function type */
typedef int (*tsc_service_cb_t)(struct tsc_service *, void *);

/* define the service structure */
typedef struct tsc_service {
  const char *sname;
  uint64_t suniqid;
  uint32_t stype;
  int port;
  int socket;
  uint64_t timeout;
  bool auto_start;
  uint32_t clients;  // total number of the connected clients
  tsc_chan_t inbound;
  tsc_chan_t outbound;
  tsc_chan_t quit;
  tsc_service_cb_t routine;
  tsc_service_cb_t init_callback;
  tsc_service_cb_t error_callback;
  tsc_service_cb_t timeout_callback;
} __attribute__((packed)) tsc_service_t;

typedef struct {
  int type;
  union {
    int socket;
    tsc_chan_t chan;
  };
} tsc_serial_t;

typedef struct tsc_conn {
  tsc_serial_t serial;
  tsc_chan_t inbound;
  tsc_chan_t outbound;
} *tsc_conn_t;

/* FIXME: the definition of common message structure */
typedef struct tsc_message {
  int len;
  tsc_serial_t serial;
  bool auto_delete;
  char *buf;
} *tsc_message_t;

extern struct tsc_service *__start_tsc_services_section;
extern struct tsc_service *__stop_tsc_services_section;

#define TSC_SERVICE_BEGIN() (tsc_service_t **)(&__start_tsc_services_section)
#define TSC_SERVICE_END() (tsc_service_t **)(&__stop_tsc_services_section)

#define TSC_SERVICE_DECLARE_LOCAL(_service, _name, _timeout, _auto, _routine, \
                                  _init_cb, _error_cb, _timeout_cb)           \
  struct tsc_service _service = {.sname = _name,                              \
                                 .stype = TSC_SERVICE_LOCAL,                  \
                                 .timeout = _timeout,                         \
                                 .auto_start = _auto,                         \
                                 .routine = _routine,                         \
                                 .init_callback = _init_cb,                   \
                                 .error_callback = _error_cb,                 \
                                 .timeout_callback = _timeout_cb};            \
  struct tsc_service *__attribute((__section__("tsc_services_section")))      \
  __attribute((__used__)) _service##_address = &(_service)

#define TSC_SERVICE_DECLARE_TCP(_service, _name, _timeout, _auto, _port,    \
                                _routine, _init_cb, _error_cb, _timeout_cb) \
  struct tsc_service _service = {                                           \
      .sname = _name,                                                       \
      .stype = TSC_SERVICE_LOCAL | TSC_SERVICE_REMOTE_TCP,                  \
      .port = _port,                                                        \
      .timeout = _timeout,                                                  \
      .auto_start = _auto,                                                  \
      .routine = _routine,                                                  \
      .init_callback = _init_cb,                                            \
      .error_callback = _error_cb,                                          \
      .timeout_callback = _timeout_cb};                                     \
  struct tsc_service *__attribute((__section__("tsc_services_section")))    \
  __attribute((__used__)) _service##_address = &(_service)

/* the public interfaces for server end */
int tsc_init_all_services(void);
int tsc_service_register(tsc_service_t *service, int uniqid);
int tsc_service_start(tsc_service_t *service, bool backend);
int tsc_service_reload(tsc_service_t *service);
int tsc_service_quit(tsc_service_t *service, int info);

/* the public interfaces for client end */
tsc_conn_t tsc_service_connect_by_name(const char *);
tsc_conn_t tsc_service_connect_by_addr(uint32_t, int);
tsc_conn_t tsc_service_connect_by_host(const char *, int);

int tsc_service_disconnect(tsc_conn_t conn);

int tsc_service_lookup(const char *, uint32_t *ip, int *port);  // TODO

/* the public interfaces for messages passing */
int tsc_message_init(tsc_message_t*, tsc_conn_t, int, bool);
int tsc_message_send(tsc_message_t*, tsc_conn_t);
int tsc_message_recv(tsc_message_t*, tsc_conn_t);

#endif  // _TSC_SERVICE_H_
