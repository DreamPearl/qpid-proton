/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "test_bits.hpp"

#include <proton/connection.hpp>
#include <proton/connection_options.hpp>
#include <proton/container.hpp>
#include <proton/delivery.h>
#include <proton/delivery.hpp>
#include <proton/link.hpp>
#include <proton/listen_handler.hpp>
#include <proton/listener.hpp>
#include <proton/message.hpp>
#include <proton/messaging_handler.hpp>

#include <proton/connection.h>
#include <proton/condition.h>
#include <proton/delivery.h>
#include <proton/link.h>
#include <proton/message.h>
#include <proton/proactor.h>
#include <proton/session.h>
#include <proton/transport.h>

#include <iostream> // TODO: Remove

#include <condition_variable>
#include <mutex>
#include <thread>

namespace {
std::mutex m;
std::condition_variable cv;
bool listener_ready = false;
int listener_port;
} // namespace

typedef struct app_data_t {
  const char *host, *port;
  const char *amqp_address;
  const char *container_id;
  int message_count;

  pn_proactor_t *proactor;
  pn_rwbytes_t message_buffer;
  int sent;
  int aborted;
  bool in_progress;
} app_data_t;

static int exit_code = 0;

#define PN_MAX_ADDR 1060
#define MSG_SIZE 80000
#define HOLDBACK  1000

class test_server : public proton::messaging_handler {
  private:
    class listener_ready_handler : public proton::listen_handler {
        void on_open(proton::listener &l) override {
            {
                std::lock_guard<std::mutex> lk(m);
                listener_port = l.port();
                listener_ready = true;
            }
            cv.notify_one();
            std::cout << "Notified the main thread that the listener is active." << std::endl;
        }

        // proton::connection_options on_accept(proton::listener &l) override {
        //     l.stop();
        //     proton::connection_options co;
        //     return co;
        // }
    };

    std::string url;
    proton::listener listener;
    listener_ready_handler listen_handler;

  public:
    test_server (const std::string &s) : url(s) {}

    void on_container_start(proton::container &c) override {
        std::cout << "Receiver container started." << std::endl;
        listener = c.listen(url, listen_handler);
    }

    void on_message(proton::delivery &d, proton::message &msg) override {
        std::cout << "I am a message and I am delivered." << std::endl;
        // d.receiver().close();
        // d.connection().close();
        // listener.stop();
    }

    void on_delivery_settle(proton::delivery &d) override {
        std::cout << "Delivery settled receiver." << std::endl;
        d.receiver().close();
        d.connection().close();
        // listener.stop();
    }
};

static void check_condition(pn_event_t *e, pn_condition_t *cond) {
  if (pn_condition_is_set(cond)) {
    fprintf(stderr, "%s: %s: %s\n", pn_event_type_name(pn_event_type(e)),
            pn_condition_get_name(cond), pn_condition_get_description(cond));
    pn_connection_close(pn_event_connection(e));
    exit_code = 1;
  }
}

/* Create a message with a map { "sequence" : number } encode it and return the encoded buffer. */
static pn_bytes_t encode_message(app_data_t* app) {
  /* Construct a message with the map { "sequence": app.sent } */
  pn_message_t* message = pn_message();
  char data[MSG_SIZE + 11] = {0};
  pn_data_t* body;
  // pn_message_set_id(message, (pn_atom_t){.type=PN_ULONG, .u.as_ulong=app->sent});
  std::cout << "Inside encode message" << std::endl;
  body = pn_message_body(message);
  pn_data_enter(body);
  pn_data_put_string(body, pn_bytes(MSG_SIZE, data));
  pn_data_exit(body);

  /* encode the message, expanding the encode buffer as needed */
  if (app->message_buffer.start == NULL) {
    static const size_t initial_size = MSG_SIZE + 1000;
    app->message_buffer = pn_rwbytes(initial_size, (char*)malloc(initial_size));
  }
  /* app->message_buffer is the total buffer space available. */
  /* mbuf wil point at just the portion used by the encoded message */
  {
  pn_rwbytes_t mbuf = pn_rwbytes(app->message_buffer.size, app->message_buffer.start);
  int status = 0;
  while ((status = pn_message_encode(message, mbuf.start, &mbuf.size)) == PN_OVERFLOW) {
    app->message_buffer.size *= 2;
    app->message_buffer.start = (char*)realloc(app->message_buffer.start, app->message_buffer.size);
    mbuf.size = app->message_buffer.size;
  }
  if (status != 0) {
    fprintf(stderr, "error encoding message: %s\n", pn_error_text(pn_message_error(message)));
    exit(1);
  }
  pn_message_free(message);
  return pn_bytes(mbuf.size, mbuf.start);
  }
}

/* Returns true to continue, false if finished */
static bool handle(app_data_t* app, pn_event_t* event) {
  switch (pn_event_type(event)) {

   case PN_CONNECTION_INIT: {
     pn_connection_t* c = pn_event_connection(event);
     pn_session_t* s = pn_session(pn_event_connection(event));
     pn_connection_set_container(c, app->container_id);
     pn_connection_open(c);
     pn_session_open(s);
     {
     pn_link_t* l = pn_sender(s, "my_sender");
     pn_terminus_set_address(pn_link_target(l), app->amqp_address);
     pn_link_open(l);
     break;
     }
   }

   case PN_LINK_FLOW: {
     /* The peer has given us some credit, now we can send messages */
     pn_link_t *sender = pn_event_link(event);
     while (app->in_progress || (pn_link_credit(sender) > 0 && app->sent < app->message_count)) {
        if (!app->in_progress) {
          pn_bytes_t msgbuf = encode_message(app);
          /* Use sent counter as unique delivery tag. */
          pn_delivery(sender, pn_dtag((const char *)&app->sent, sizeof(app->sent)));
          pn_link_send(sender, msgbuf.start, msgbuf.size - HOLDBACK); /* Send some part of message */
          std::cout << "Sent some part of the message" << std::endl;
          app->in_progress = true;
          /* Return from this link flow event and abort the message on future FLOW, */
          break;
        } else {
          pn_delivery_t * pnd = pn_link_current(sender);
          if (pn_delivery_pending(pnd) == 0) {
            // All message data from pn_link_send has been processed to physical frames.
            pn_delivery_abort(pnd);
            std::cout << "Aborted the delivery" << std::endl;
            /* aborted delivery is presettled and never ack'd. */
            if (++app->aborted == app->message_count) {
              printf("%d messages started and aborted\n", app->aborted);
              pn_connection_close(pn_event_connection(event));
              /* Continue handling events till we receive TRANSPORT_CLOSED */
            }
            ++app->sent;
            app->in_progress = false;
          } else {
            // Keep checking FLOW events until all message data forwarded to peer.
            break;
          }
        }
     }
     break;
   }

   case PN_DELIVERY: {
     /* We received acknowledgement from the peer that a message was delivered. */
     pn_delivery_t* d = pn_event_delivery(event);
     fprintf(stderr, "Aborted deliveries should not receive delivery events. Delivery state %d\n", (int)pn_delivery_remote_state(d));
     pn_connection_close(pn_event_connection(event));
     exit_code=1;
     break;
   }

   case PN_TRANSPORT_CLOSED:
    std::cout << "Sender PN_TRANSPORT_CLOSED." << std::endl;
    check_condition(event, pn_transport_condition(pn_event_transport(event)));
    break;

   case PN_CONNECTION_REMOTE_CLOSE:
    check_condition(event, pn_connection_remote_condition(pn_event_connection(event)));
    pn_connection_close(pn_event_connection(event));
    break;

   case PN_SESSION_REMOTE_CLOSE:
    check_condition(event, pn_session_remote_condition(pn_event_session(event)));
    pn_connection_close(pn_event_connection(event));
    break;

   case PN_LINK_REMOTE_CLOSE:
   case PN_LINK_REMOTE_DETACH:
    check_condition(event, pn_link_remote_condition(pn_event_link(event)));
    pn_connection_close(pn_event_connection(event));
    break;

   case PN_PROACTOR_INACTIVE:
    return false;

   default: break;
  }
  return true;
}

void run(app_data_t *app) {
  /* Loop and handle events */
  do {
    pn_event_batch_t *events = pn_proactor_wait(app->proactor);
    pn_event_t* e;
    for (e = pn_event_batch_next(events); e; e = pn_event_batch_next(events)) {
      if (!handle(app, e)) {
        return;
      }
    }
    pn_proactor_done(app->proactor, events);
  } while(true);
}

int test_aborted_msg() {

    //  CPP Receiver
    std::string recv_address("127.0.0.1:amqp/test");
    test_server recv(recv_address);
    proton::container c(recv);
    std::cout << "Run the listener" << std::endl;
    std::thread thread_recv([&c]() -> void { c.run(); });

    // wait until listener is ready
    std::unique_lock<std::mutex> lk(m);
    std::cout << "Waiting for listener to get ready" << std::endl;
    cv.wait(lk, [] { return listener_ready; });

    // C Sender
    struct app_data_t app = {0};
    char addr[PN_MAX_ADDR];
    app.host = "127.0.0.1";
    app.port = "amqp";
    app.amqp_address = "test";
    app.message_count = 1;

    app.proactor = pn_proactor();
    pn_proactor_addr(addr, sizeof(addr), app.host, app.port);
    pn_proactor_connect2(app.proactor, NULL, NULL, addr);
    std::cout << "Run the sender" << std::endl;
    run(&app);

    std::cout << "Before the thread join" << std::endl;
    thread_recv.join();
    std::cout << "After the thread join" << std::endl;
  
    pn_proactor_free(app.proactor);
    free(app.message_buffer.start);
    return 0;
}

int main(int argc, char **argv) {
    int failed = 0;
    std::cout << "Inside main" << std::endl;
    RUN_ARGV_TEST(failed, test_aborted_msg());
    return failed;
}
