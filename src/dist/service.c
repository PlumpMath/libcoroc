#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "service.h"
#include "../time.h"

struct tsc_client_param {
  int socket;
  tsc_chan_t comm;
};

static int __tsc_message_pump(tsc_service_t *service) {
  struct tsc_message *message;
  int info, error;

  tsc_chan_t _chan = NULL;
  tsc_timer_t timer = tsc_timer_allocate(0, 0);
  tsc_chan_set_t set = tsc_chan_set_allocate(3);

  // add the channels to the set
  tsc_chan_t comm = tsc_refcnt_get(service->comm);
  tsc_chan_t quit = tsc_refcnt_get(service->quit);

  tsc_chan_set_recv(set, comm, &message);
  tsc_chan_set_recv(set, quit, &info);
  tsc_chan_set_recv(set, (tsc_chan_t)timer, 0);

  // start the select loop ..
  for (;;) {
    // set the timer ..
    if (service->timeout != TSC_SERVICE_INFINITY)
      tsc_timer_after(timer, service->timeout);

    tsc_chan_set_select(set, &_chan);
    if (comm == _chan) {
      if (message == NULL) {
        TSC_ATOMIC_DEC(service->clients);
        error = -1;  // client disconnect ..
      } else {
        error = service->routine(service, message);
      }
      if (error && service->error_callback)
        service->error_callback(service, &error);
      // the message should be freed here!!
      tsc_message_dealloc(message);
    } else if ((tsc_chan_t)timer == _chan) {
      service->timeout_callback(service, NULL);
    } else if (quit == _chan) {
      goto __tsc_message_pump_exit;
    }
  }

__tsc_message_pump_exit:
  // release the resources ..
  tsc_chan_set_dealloc(set);
  tsc_timer_stop(timer);
  tsc_timer_dealloc(timer);

  // close the channels ..
  tsc_chan_close(comm);
  tsc_chan_close(quit);

  // drop the references ..
  tsc_refcnt_put(comm);
  tsc_refcnt_put(quit);

  if (service->stype & TSC_SERVICE_REMOTE_TCP) {
    // TODO: notify the service side to quit !!
    close(service->socket);
  }

  return 0;
}

static int __tsc_parse_message(struct tsc_client_param *param) {
  int32_t len;
  struct tsc_message *message;

  while (tsc_net_read(param->socket, &len, sizeof(int32_t)) > 0) {
    if (len == 0) continue;

    if (len < 0) {
      // a negtive len means the remote client decide to disconnect
      message = NULL;
    } else {
      // alloc new message buffer ..
      message = tsc_message_alloc(len);
      // read the result of the package content ..
      assert(tsc_net_read(param->socket, message->buf, len) == len);
    }

    // dispatch the message to the `pump' task ..
    if (tsc_chan_send(param->comm, &message) == CHAN_CLOSED) break;
  }

  // dealloc the resources ..
  tsc_refcnt_put(param->comm);
  close(param->socket);
  TSC_DEALLOC(param);  // !!

  return 0;
}

static int __tsc_server_loop(tsc_service_t *service) {
  // start the server ..
  int socket = tsc_net_announce(true, "*", service->port);
  assert(socket >= 0);

  // loop, waiting for new clients ..
  for (;;) {
    int client = tsc_net_accept(socket, NULL, NULL);
    if (client < 0) break;

    // start a new task to serve this client..
    struct tsc_client_param *param;
    param = TSC_ALLOC(sizeof(*param));

    param->socket = client;
    param->comm = tsc_refcnt_get(service->comm);

    TSC_ATOMIC_INC(service->clients);
    tsc_coroutine_spawn(__tsc_parse_message, param, "parsing");
  }

  // TODO: how to notice the server to exit ??
  return 0;
}

static int __tsc_client_loop(struct tsc_client_param *param) {
  struct tsc_message *message = NULL;
  uint32_t sig = -1;

  for (;;) {
    // get the message element from the send operations ..
    tsc_chan_recv(param->comm, &message);
    if (message == NULL) {
      // if the client close the channel, we just send a negtive sized message
      // ..
      tsc_net_write(param->socket, &sig, sizeof(uint32_t));
      break;
    } else {
      // firstly, sending the length of this message
      assert(tsc_net_write(param->socket, &(message->len), sizeof(uint32_t)) ==
             sizeof(uint32_t));
      // and then, sending the content of this message
      assert(tsc_net_write(param->socket, message->buf, message->len) ==
             message->len);

      // release the local message ..
      tsc_message_dealloc(message);
    }
  }

  // release the resources!!
  close(param->socket);
  tsc_chan_close(param->comm);
  tsc_refcnt_put(param->comm);
  TSC_DEALLOC(param);

  return 0;
}

int tsc_service_register(tsc_service_t *service, int uniqid) {
  assert(service && uniqid >= 0);
  service->suniqid = uniqid;
  if (service->stype & TSC_SERVICE_REMOTE_TCP)
    service->suniqid |= (service->port << 16);
  return 0;
}

int tsc_service_start(tsc_service_t *service, bool backend) {
  assert(service != NULL);

  // create the internal channel for messages ..
  service->comm = tsc_refcnt_get(tsc_chan_allocate(sizeof(void *), 0));
  // create the channel for quiting ..
  service->quit = tsc_refcnt_get(tsc_chan_allocate(sizeof(int), 1));

  // create a coroutine to establish the TCP server..
  if (service->stype & TSC_SERVICE_REMOTE_TCP)
    tsc_coroutine_spawn(__tsc_server_loop, (void *)service, "tcp");

  // create a coroutine to service the message channel..
  if (backend) {
    tsc_coroutine_spawn(__tsc_message_pump, (void *)service, "pump");
  } else {
    __tsc_message_pump(service);
  }
  return 0;
}

int tsc_service_quit(tsc_service_t *service, int info) {
  assert(service && service->quit);
  tsc_chan_send(service->quit, &info);
  tsc_refcnt_put(service->quit);
  return 0;
}

int tsc_service_reload(tsc_service_t *service) {
  tsc_service_quit(service, 0);
  tsc_service_start(service, true);  // TODO
  return 0;
}

// FIXME: using a hash map to enhance the searching ..
tsc_chan_t tsc_service_connect_by_name(const char *name) {
  tsc_service_t **iter = TSC_SERVICE_BEGIN();
  for (; iter < TSC_SERVICE_END(); ++iter) {
    if (!strcmp((*iter)->sname, name)) {
      TSC_ATOMIC_INC((*iter)->clients);
      return tsc_refcnt_get((*iter)->comm);
    }
  }
  return NULL;
}

tsc_chan_t tsc_service_connect_by_addr(uint32_t addr, int port) {
  char server[16];
  uint8_t *ip = (uint8_t *)(&addr);
  snprintf(server, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return tsc_service_connect_by_host(server, port);
}

tsc_chan_t tsc_service_connect_by_host(const char *host, int port) {
  // FIXME: we don't support the multi-param passing to a new spawned task now!
  struct tsc_client_param *param;
  tsc_chan_t comm = NULL;
  param = TSC_ALLOC(sizeof(*param));

  param->socket = tsc_net_dial(true, host, port);
  if (param->socket < 0) {
    TSC_DEALLOC(param);
  } else {
    comm = tsc_chan_allocate(sizeof(void *), 0);
    param->comm = tsc_refcnt_get(comm);

    // NOTE: param MUST be freed by the new task..
    tsc_coroutine_spawn(__tsc_client_loop, param, "client");
  }
  return comm;
}

// FIXME: we need to just define one service to create the section
TSC_SERVICE_DECLARE_LOCAL(__dummy_service, "dummy", TSC_SERVICE_INFINITY, false,
                          NULL, NULL, NULL, NULL);

// start all the services ..
int tsc_init_all_services(void) {
  int id = 0;
  tsc_service_t **ser = TSC_SERVICE_BEGIN();

  for (; ser < TSC_SERVICE_END(); ++ser, ++id) {
    tsc_service_register(*ser, id);
    if ((*ser)->init_callback) (*ser)->init_callback(*ser, &id);
    if ((*ser)->auto_start) tsc_service_start(*ser, true);
  }

  return id;
}

int tsc_service_disconnect(tsc_chan_t conn) {
  struct tsc_message *message = NULL;
  tsc_chan_send(conn, &message);
  tsc_refcnt_put(conn);
  return 0;
}

/* the implement of message api */
struct tsc_message *tsc_message_alloc(int len) {
  if (len < 0) return NULL;

  tsc_message_t message = NULL;
  message = TSC_ALLOC(sizeof(struct tsc_message) + len);
  if (message != NULL) message->len = len;
  return message;
}

void tsc_message_dealloc(struct tsc_message *message) { TSC_DEALLOC(message); }
