/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015 Treasure Data Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <unistd.h>
#include <fluent-bit/flb_utils.h>
#include <fluent-bit/flb_engine.h>
#include <fluent-bit/flb_network.h>

#include "mqtt.h"
#include "mqtt_prot.h"
#include "mqtt_conn.h"

/* Callback invoked every time an event is triggered for a connection */
int mqtt_conn_event(void *data)
{
    int ret;
    int bytes;
    struct mk_event *event;
    struct mqtt_conn *conn = data;

    event = &conn->event;
    if (event->mask & MK_EVENT_READ) {
        bytes = read(conn->fd, conn->buf, sizeof(conn->buf));
        if (bytes > 0) {
            conn->buf_len = bytes;
            flb_debug("[mqtt] %i bytes in", bytes);
            ret = mqtt_prot_parser(conn);
            if (ret == MQTT_ERROR) {
                flb_debug("[mqtt] fd=%i protocol error", event->fd);
            }
            else if (ret == MQTT_HANGUP) {
                flb_debug("[mqtt] fd=%i client hangup", event->fd);
            }

            if (ret < 0) {
                mqtt_conn_del(conn);
                return -1;
            }
        }
        else {
            flb_debug("[mqtt] fd=%i closed connection", event->fd);
            mqtt_conn_del(conn);
        }
    }
    else if (event->mask & MK_EVENT_CLOSE) {
        flb_debug("[mqtt] fd=%i hangup", event->fd);
    }
    return 0;
}

/* Create a new mqtt request instance */
struct mqtt_conn *mqtt_conn_add(int fd, struct flb_in_mqtt_config *ctx)
{
    int ret;
    struct mqtt_conn *conn;
    struct mk_event *event;

    conn = malloc(sizeof(struct mqtt_conn));
    if (!conn) {
        return NULL;
    }

    /* Set data for the event-loop */
    event = &conn->event;
    event->fd           = fd;
    event->type         = FLB_ENGINE_EV_CUSTOM;
    event->mask         = MK_EVENT_EMPTY;
    event->handler      = mqtt_conn_event;
    event->status       = MK_EVENT_NONE;

    /* Connection info */
    conn->fd      = fd;
    conn->ctx     = ctx;
    conn->buf_pos = 0;
    conn->buf_len = 0;
    conn->status  = MQTT_NEW;

    /* Register instance into the event loop */
    ret = mk_event_add(ctx->evl, fd, FLB_ENGINE_EV_CUSTOM, MK_EVENT_READ, conn);
    if (ret == -1) {
        flb_error("[mqtt] could not register new connection");
        close(fd);
        free(conn);
        return NULL;
    }

    return conn;
}

int mqtt_conn_del(struct mqtt_conn *conn)
{
    /* Unregister the file descriptior from the event-loop */
    mk_event_del(conn->ctx->evl, &conn->event);

    /* Release resources */
    close(conn->fd);
    free(conn);

    return 0;
}
