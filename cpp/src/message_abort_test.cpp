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

#include <catch.hpp>
#include "./pn_test.hpp"

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
#include <proton/link.h>
#include <proton/session.h>

#include <iostream>

namespace {
bool listener_ready = false;
int listener_port;
} // namespace


class test_server : public proton::messaging_handler {
  private:
    class listener_ready_handler : public proton::listen_handler {
        void on_open(proton::listener &l) override {
            {
                listener_port = l.port();
            }
        }
    };

    std::string url;
    proton::listener listener;
    listener_ready_handler listen_handler;

  public:
    test_server (const std::string &s) : url(s) {}

    void on_container_start(proton::container &c) override {
        listener = c.listen(url, listen_handler);
    }

    void on_message(proton::delivery &d, proton::message &msg) override {
        std::cout << "I am a message and I am delivered." << std::endl;
        // ASSERT_EQUAL(test_tag, d.tag());
        d.receiver().close();
        d.connection().close();
        listener.stop();
    }

    void on_delivery_settle(proton::delivery &d) override {
        d.receiver().close();
        d.connection().close();
    }
};

/* Handler that opens a connection and sender link */
struct send_client_handler : public pn_test::handler {
  bool handle(pn_event_t *e) {
    switch (pn_event_type(e)) {
    case PN_CONNECTION_LOCAL_OPEN: {
      pn_connection_open(pn_event_connection(e));
      pn_session_t *ssn = pn_session(pn_event_connection(e));
      pn_session_open(ssn);
      pn_link_t *snd = pn_sender(ssn, "x");
      pn_link_open(snd);
      break;
    }
    case PN_LINK_REMOTE_OPEN: {
      link = pn_event_link(e);
      return true;
    }
    default:
      break;
    }
    return false;
  }
};

TEST_CASE("driver_message_abort") {
  send_client_handler client;
  test_server server("127.0.0.1:0/test");
  pn_test::driver_pair d(client, server);

//   d.run();
//   pn_link_t *rcv = server.link;
  pn_link_t *snd = client.link;
  char data[100] = {0};          /* Dummy data to send. */
  char rbuf[sizeof(data)] = {0}; /* Read buffer for incoming data. */

  /* Send 2 frames with data */
//   pn_link_flow(rcv, 1);
//   CHECK(1 == pn_link_credit(rcv));
//   d.run();
//   CHECK(1 == pn_link_credit(snd));
  pn_delivery_t *sd = pn_delivery(snd, pn_bytes("1")); /* Sender delivery */
  pn_delivery_t *rd = server.delivery;
/* Abort the delivery */
  pn_delivery_abort(sd);
  CHECK(pn_delivery_aborted(rd));
}