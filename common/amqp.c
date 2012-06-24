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

#include <amqp.h>

#include "common.h"

int	 amqp_log_error(int);
int	 amqp_log_amqp_error(amqp_rpc_reply_t);

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

struct amqp *
amqp_init(void)
{
	struct amqp	*env;

	if ((env = calloc(1, sizeof(struct amqp))) == NULL)
		return (NULL);

	LIST_INIT(&env->bindings);

	return (env);
}

int
amqp_open(struct amqp *env)
{
	int	 fd;

	/* Create a connection, log in and create the (only) channel */
	env->c = amqp_new_connection();
	if (amqp_log_error(fd = amqp_open_socket(env->host, env->port)) != 0)
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
amqp_exchange(struct amqp *env)
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
amqp_queue(struct amqp *env)
{
	amqp_table_entry_t	 e[2];
	amqp_table_t		 t;
	struct binding		*b;
	int			 i;

	/* Declare the queue */
	i = 0;
	if (env->flags & AMQP_FLAG_MIRRORED_QUEUE) {
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

	/* Prefetch QoS */
	if (env->prefetch > 0) {
		amqp_basic_qos(env->c, AMQP_DEFAULT_CHANNEL, 0,
		    env->prefetch, 0);
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

int
amqp_consume(struct amqp *env, char **key, char **buf, size_t *len)
{
	amqp_frame_t		 f;
	amqp_basic_deliver_t	*d;
	int			 r;
	size_t			 l;

	amqp_maybe_release_buffers(env->c);
	if (((r = amqp_simple_wait_frame(env->c, &f)) < 0) ||
	    (f.frame_type != AMQP_FRAME_METHOD) ||
	    (f.payload.method.id != AMQP_BASIC_DELIVER_METHOD))
		return (-1);
	d = (amqp_basic_deliver_t *)f.payload.method.decoded;

#if 0
	log_debug("delivery %u, exchange %.*s, routing key %.*s",
	    d->delivery_tag,
	    (int)d->exchange.len, (char *)d->exchange.bytes,
	    (int)d->routing_key.len, (char *)d->routing_key.bytes);
#endif

	/* Add on an extra byte to null-terminate the string */
	if ((*key = calloc(1, d->routing_key.len + 1)) == NULL)
		fatal("amqp_consume");
	memcpy(*key, d->routing_key.bytes, d->routing_key.len);

	if (((r = amqp_simple_wait_frame(env->c, &f)) < 0) ||
	    (f.frame_type != AMQP_FRAME_HEADER)) {
		free(*key);
		return (-1);
	}
	*len = f.payload.properties.body_size;

	/* Add on an extra byte to null-terminate the string */
	if ((*buf = calloc(1, *len + 1)) == NULL)
		fatal("amqp_consume");

	l = 0;
	while (l < *len) {
		if (((r = amqp_simple_wait_frame(env->c, &f)) < 0) ||
		    (f.frame_type != AMQP_FRAME_BODY)) {
			free(*key);
			free(*buf);
			return (-1);
		}
		memcpy(*buf + l, f.payload.body_fragment.bytes,
		    f.payload.body_fragment.len);
		l += f.payload.body_fragment.len;
	}

	/* XXX What happens when this wraps? */
	return (d->delivery_tag);
}

void
amqp_publish(struct amqp *env, char *key, char *data)
{
	amqp_bytes_t		 msg;
	amqp_basic_properties_t	 p;

	/* Persistent messages */
	p._flags = AMQP_BASIC_DELIVERY_MODE_FLAG;
	p.delivery_mode = 2;

	msg.len = strlen(data);
	msg.bytes = data;

	log_debug("publishing %d bytes", strlen(data));

	amqp_basic_publish(env->c, AMQP_DEFAULT_CHANNEL,
	    amqp_cstring_bytes(env->exchange), amqp_cstring_bytes(key), 0, 0,
	    &p, msg);
}

void
amqp_acknowledge(struct amqp *env, int tag)
{
	amqp_basic_ack(env->c, AMQP_DEFAULT_CHANNEL, tag, 0);
}

void
amqp_reject(struct amqp *env, int tag, int requeue)
{
	amqp_basic_reject(env->c, AMQP_DEFAULT_CHANNEL, tag, requeue);
}

void
amqp_close(struct amqp *env)
{
	amqp_log_amqp_error(amqp_channel_close(env->c, AMQP_DEFAULT_CHANNEL,
	    AMQP_REPLY_SUCCESS));
	amqp_log_amqp_error(amqp_connection_close(env->c, AMQP_REPLY_SUCCESS));
	amqp_log_error(amqp_destroy_connection(env->c));
}
