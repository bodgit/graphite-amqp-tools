/*
 * Copyright (c) 2012 Matt Dainty <matt@bodgit-n-scarper.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>

#include "common.h"

int
amqp_log_error(int r)
{
	char	*s;

	if (r < 0) {
		s = amqp_error_string(-r);
		log_warnx("%s", s);
		free(s);
		return (-1);
	}

	return (0);
}

int
amqp_log_amqp_error(amqp_rpc_reply_t r)
{
	char			*s;
	amqp_connection_close_t	*co;
	amqp_channel_close_t	*ch;

	switch (r.reply_type) {
	case AMQP_RESPONSE_NORMAL:
		return (0);
		break;
	case AMQP_RESPONSE_NONE:
		log_warnx("missing RPC reply");
		break;
	case AMQP_RESPONSE_LIBRARY_EXCEPTION:
		s = amqp_error_string(r.library_error);
		log_warnx("%s", s);
		free(s);
		break;
	case AMQP_RESPONSE_SERVER_EXCEPTION:
		switch (r.reply.id) {
		case AMQP_CONNECTION_CLOSE_METHOD:
			co = (amqp_connection_close_t *)r.reply.decoded;
			log_warnx("server connection error %d, message: %.*s",
			    co->reply_code,
			    (int)co->reply_text.len,
			    (char *)co->reply_text.bytes);
			break;
		case AMQP_CHANNEL_CLOSE_METHOD:
			ch = (amqp_channel_close_t *)r.reply.decoded;
			log_warnx("server channel error %d, message: %.*s",
			    ch->reply_code,
			    (int)ch->reply_text.len,
			    (char *)ch->reply_text.bytes);
			break;
		default:
			log_warnx("unknown server error, method id 0x%08X",
			    r.reply.id);
			break;
		}
	}

	return (-1);
}

int
amqp_open(struct dequeue *env)
{
	int			 fd;

	/* Create a connection, log in and create the (only) channel */
	env->c = amqp_new_connection();
	if (amqp_log_error(fd = amqp_open_socket(AMQP_DEFAULT_HOST,
	    env->amqp_port)) != 0)
		goto bad;
	amqp_set_sockfd(env->c, fd);
	if (amqp_log_amqp_error(amqp_login(env->c, env->vhost, 0, 131072, 0,
	    AMQP_SASL_METHOD_PLAIN, env->user, env->password)) != 0)
		goto bad;
	amqp_channel_open(env->c, AMQP_DEFAULT_CHANNEL);
	if (amqp_log_amqp_error(amqp_get_rpc_reply(env->c)) != 0)
		goto bad;

	return (0);
bad:
	return (-1);
}

int
amqp_exchange(struct dequeue *env)
{
	amqp_table_entry_t	 e[2];
	amqp_table_t		 t;

	/* Declare the exchange */
	if (env->upstreams != NULL) {
		/* Support RabbitMQ federated exchanges which relies on the
		 * bulk of the configuration being defined statically in the
		 * configuration file, we just need the name of the set of
		 * upstream brokers
		 */
		e[0].key = amqp_cstring_bytes("upstream-set");
		e[0].value.kind = AMQP_FIELD_KIND_UTF8;
		e[0].value.value.bytes = amqp_cstring_bytes(env->upstreams);

		e[1].key = amqp_cstring_bytes("type");
		e[1].value.kind = AMQP_FIELD_KIND_UTF8;
		e[1].value.value.bytes = amqp_cstring_bytes(env->type);

		t.num_entries = 2;
		t.entries = e;

		amqp_exchange_declare(env->c, AMQP_DEFAULT_CHANNEL,
		    amqp_cstring_bytes(env->exchange),
		    amqp_cstring_bytes("x-federation"), 0, 1, t);
	} else
		amqp_exchange_declare(env->c, AMQP_DEFAULT_CHANNEL,
		    amqp_cstring_bytes(env->exchange),
		    amqp_cstring_bytes(env->type), 0, 1, amqp_empty_table);
	if (amqp_log_amqp_error(amqp_get_rpc_reply(env->c)) != 0)
		goto bad;

	return (0);
bad:
	return (-1);
}

int
amqp_queue(struct dequeue *env)
{
	amqp_table_entry_t	 e[2];
	amqp_table_t		 t;
	struct binding		*b;
	int			 i;

	/* Declare the queue */
	i = 0;
	if (env->flags & DEQUEUE_FLAG_MIRRORED_QUEUE) {
		/* Support RabbitMQ extension for HA mirrored queues across
		 * clusters, currently only mirroring across all nodes is
		 * implemented
		 */
		e[i].key = amqp_cstring_bytes("x-ha-policy");
		e[i].value.kind = AMQP_FIELD_KIND_UTF8;
		e[i].value.value.bytes = amqp_cstring_bytes("all");
		i++;
	}

	if (env->ttl > 0) {
		/* Support RabbitMQ extension for setting a TTL on a queue */
		e[i].key = amqp_cstring_bytes("x-message-ttl");
		e[i].value.kind = AMQP_FIELD_KIND_I64;
		e[i].value.value.i64 = env->ttl;
		i++;
	}

	t.num_entries = i;
	t.entries = e;

	amqp_queue_declare(env->c, AMQP_DEFAULT_CHANNEL,
	    amqp_cstring_bytes(env->queue), 0, 1, 0, 0,
	    (i > 0) ? t : amqp_empty_table);
	if (amqp_log_amqp_error(amqp_get_rpc_reply(env->c)) != 0)
		goto bad;

	/* Bind the queue to the exchange */
	if (strcmp(env->type, AMQP_EXCHANGE_TYPE_FANOUT) == 0) {
		/* Handle fanout exchanges differently (no routing key) */
		amqp_queue_bind(env->c, AMQP_DEFAULT_CHANNEL, 
		    amqp_cstring_bytes(env->queue),
		    amqp_cstring_bytes(env->exchange), amqp_empty_bytes,
		    amqp_empty_table);
		if (amqp_log_amqp_error(amqp_get_rpc_reply(env->c)) != 0)
			goto bad;
	} else
		for (b = LIST_FIRST(&env->bindings); b;
		    b = LIST_NEXT(b, entry)) {
			amqp_queue_bind(env->c, AMQP_DEFAULT_CHANNEL, 
			    amqp_cstring_bytes(env->queue),
			    amqp_cstring_bytes(env->exchange),
			    amqp_cstring_bytes(b->key), amqp_empty_table);
			if (amqp_log_amqp_error(amqp_get_rpc_reply(env->c)) != 0)
				goto bad;
		}

	amqp_basic_consume(env->c, AMQP_DEFAULT_CHANNEL,
	    amqp_cstring_bytes(env->queue), amqp_empty_bytes, 0, 0, 0,
	    amqp_empty_table);
	if (amqp_log_amqp_error(amqp_get_rpc_reply(env->c)) != 0)
		goto bad;

	return (0);
bad:
	return (-1);
}

void
amqp_close(struct dequeue *env)
{
	amqp_log_amqp_error(amqp_channel_close(env->c, AMQP_DEFAULT_CHANNEL,
	    AMQP_REPLY_SUCCESS));
	amqp_log_amqp_error(amqp_connection_close(env->c, AMQP_REPLY_SUCCESS));
	amqp_log_error(amqp_destroy_connection(env->c));
}
