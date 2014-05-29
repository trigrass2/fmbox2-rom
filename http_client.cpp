
#include "fmbox.h"

static void client_free(client_t *client) {
	if (client->url_pb)
		printbuf_free(client->url_pb);
	if (client->pb)
		printbuf_free(client->pb);
	if (client->pb2)
		printbuf_free(client->pb2);
	if (client->fp)
		fclose(client->fp);
	if (client->cookies)
		free(client->cookies);
	if (client->_header_field)
		free(client->_header_field);
	if (client->json_obj)
		json_object_put(client->json_obj);
	if (client->json_parser)
		json_tokener_free(client->json_parser);
	if (client->_host)
		free(client->_host);
}

static void on_close(uv_handle_t* handle) {
	client_t *client = (client_t *) handle->data;

	CLIENT_LOGF(client, "connection closed");
	client_free(client);
}

static void on_alloc(uv_handle_t* client, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char *)malloc(suggested_size);
	buf->len = suggested_size;
}

static void _on_data(client_t* client, const char *buf, size_t len) {
	if (client->on_data)
		client->on_data(client, buf, len);
	if (client->fp)
		fwrite(buf, 1, len, client->fp);
	if (client->json_parser)
		client->json_obj = json_tokener_parse_ex(client->json_parser, buf, len);
}

static void on_data(uv_stream_t* tcp, const char *buf, size_t len) {
	client_t* client = (client_t*)tcp->data;

	if (!client->_chunked_state) {
		_on_data(client, buf, len);
		return ;
	}

	// handle chunked data

	int data_start = -1;
	int data_len = -1;

	for (int i = 0; i < (int)len; i++) {
		char ch = buf[i];
		int v;

		switch (client->_chunked_state) {
		case CLIENT_CHUNKED_READ_LEN_CR:
			v = char_hex2dec(ch);
			if (v == -1) {
				if (client->_chunked_len == 0)
					client->_chunked_state = CLIENT_CHUNKED_READ_END;
				else if (ch == '\r')
					client->_chunked_state = CLIENT_CHUNKED_READ_LEN_LF;
			} else {
				client->_chunked_len <<= 4;
				client->_chunked_len += v;
			}
			break;
		case CLIENT_CHUNKED_READ_LEN_LF:
			if (ch == '\n')
				client->_chunked_state = CLIENT_CHUNKED_READ_DATA_CR;
			break;
		case CLIENT_CHUNKED_READ_DATA_CR:
			if (client->_chunked_len == 0) {
				if (ch == '\r')
					client->_chunked_state = CLIENT_CHUNKED_READ_DATA_LF;
				if (data_start != -1) {
					_on_data(client, buf + data_start, data_len);
					data_start = -1;
					data_len = -1;
				}
			} else {
				client->_chunked_len--;
				if (data_start == -1) {
					data_start = i;
					data_len = 0;
				}
				data_len++;
			}
			break;
		case CLIENT_CHUNKED_READ_DATA_LF:
			if (ch == '\n') {
				client->_chunked_state = CLIENT_CHUNKED_READ_LEN_CR;
				client->_chunked_len = 0;
			}
			break;
		}
	}

	if (data_start != -1)
		_on_data(client, buf + data_start, data_len);

	//LOGF("[%d] on_data: len %d", client->no, len);
}

static void on_data_done(uv_stream_t *tcp) {
	client_t* client = (client_t*)tcp->data;

	CLIENT_LOGF(client, "on_data_done");

	if (client->cookies) {
		CLIENT_LOGF(client, "cookies: %s", client->cookies);
	}

	if (client->on_data_done)
		client->on_data_done(client);
}

static int on_headers_complete(http_parser* parser) {
	client_t *client = (client_t *)parser->data;

	CLIENT_LOGF(client, "on_headers_complete");

	return 1;
}

static int on_header_field(http_parser *p, const char *buf, size_t len) {
	client_t *client = (client_t *)p->data;

	char *s = (char *)malloc(len+1);
	memcpy(s, buf, len);
	s[len] = 0;
	client->_header_field = s;

	return 0;
}


static int on_header_value(http_parser *p, const char *buf, size_t len) {
	client_t *client = (client_t *)p->data;

	char *value = (char *)malloc(len+1);
	memcpy(value, buf, len);
	value[len] = 0;

	CLIENT_LOGF(client, "on_header_value: %s: %s", client->_header_field, value);

	if (client->save_cookie && client->_header_field && !strcmp(client->_header_field, "Set-Cookie")) {
		if (str_cut(value, ';')) {
			if (client->cookies != NULL)
				client->cookies = str_append(client->cookies, "; ");
			client->cookies = str_append(client->cookies, value);
		}
	}

	if (!strcmp(client->_header_field, "Transfer-Encoding") && !strcmp(value, "chunked")) {
		client->_chunked_state = CLIENT_CHUNKED_READ_LEN_CR;
	}

	if (client->_header_field) {
		free(client->_header_field);
		client->_header_field = NULL;
	}

	free(value);

	return 0;
}

static void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t *buf) {
	client_t* client = (client_t*)tcp->data;
	ssize_t parsed;

	CLIENT_LOGF(client, "on_read: len %d", nread);

	if (nread >= 0) {
		client->parser_settings.on_header_field = on_header_field;
		client->parser_settings.on_header_value = on_header_value;
		client->parser_settings.on_headers_complete = on_headers_complete;

		parsed = http_parser_execute(
			&client->parser, &client->parser_settings, buf->base, nread);
		if (parsed < nread && parsed != 0) {
			//LOGF("[%d] parse error: %s", client->no, http_errno_description(HTTP_PARSER_ERRNO(&client->parser)));
		}
		on_data(tcp, buf->base + parsed, nread - parsed);
	} else {
		on_data_done(tcp);
		uv_close((uv_handle_t *)tcp, on_close);
	}

	free(buf->base);
}

static void after_write(uv_write_t* req, int status) {
	client_t *client = (client_t *)req->data;

	if (status) {
		CLIENT_LOGF(client, "write failed");
		return ;
	}

	http_parser_init(&client->parser, HTTP_RESPONSE);
	client->parser.data = client;

	uv_read_start((uv_stream_t *)&client->socket, on_alloc, on_read);
}

static void on_connect(uv_connect_t *stream, int status) {
	client_t *client = (client_t *)stream->data;
	int r;

	if (status) {
		CLIENT_LOGF(client, "connect failed: %d", status);
		client_free(client);
		return ;
	}

	CLIENT_LOGF(client, "connect ok");

	client->write_req.handle = (uv_stream_t *)stream;
	client->write_req.data = client;
	r = uv_write(&client->write_req, stream->handle, client->uvbuf, client->uvbuf_nr, after_write);
	if (r) {
		CLIENT_LOGF(client, "write failed: %d", r);
	}
}

static void on_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res) {
	client_t *client = (client_t *)resolver->data;

	if (!status) {
		char addr[17] = {'\0'};
		uv_ip4_name((struct sockaddr_in *) res->ai_addr, addr, 16);
		CLIENT_LOGF(client, "resolved: %s", addr);

		uv_tcp_init(uv_loop, &client->socket);
		client->connect_req.data = client;
		client->socket.data = client;

		uv_tcp_connect(&client->connect_req, (uv_tcp_t *)&client->socket, (const struct sockaddr *)res->ai_addr, on_connect);
	} else {
		CLIENT_LOGF(client, "getaddrinfo callback error: %d", status);
		client_free(client);
		return;
	}

	uv_freeaddrinfo(res);
}

static int client_no;

void client_init(client_t *client) {
	memset(client, 0, sizeof(client_t));
	client->no = client_no++;
}

void client_start(client_t *client) {
	client->resolver.data = client;

	if (client->host == NULL) {
		struct http_parser_url u = {};
		
		if (http_parser_parse_url(client->url, strlen(client->url), 0, &u))
			return ;
				client->_host = str_dup_n(
			client->url + u.field_data[UF_HOST].off,
			u.field_data[UF_HOST].len
		);
		client->pb = printbuf_new();
		sprintbuf(client->pb, 
			"GET %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"Accept: */*\r\n"
			"Connection: close\r\n"
			"User-Agent: Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6)\r\n"
			"\r\n",
			client->url + u.field_data[UF_PATH].off,
			client->_host
			);
		client->uvbuf[0].base = client->pb->buf;
		client->uvbuf[0].len = printbuf_length(client->pb);
		client->uvbuf_nr = 1;
		CLIENT_LOGF(client, "resolve %s", client->_host);

		uv_getaddrinfo(uv_loop, &client->resolver, on_resolved, client->_host, "80", NULL);
	} else {
		CLIENT_LOGF(client, "resolve %s", client->host);
		uv_getaddrinfo(uv_loop, &client->resolver, on_resolved, client->host, "80", NULL);
	}
}

static void test_echo_done(client_t *c, char *text) {
	if (strcmp(text, "month=6&day=4"))
}

static int test_echo() {
	client_t c = {};

	c.url_pb = printfbuf_new();
	sprintbuf(c.url_pb, "http://localhost:1989/echo?month=%d&day=%d", 6, 4);
	c.on_text_done = test_echo_done;

	client_start(&c);

	return 0;
}

void unit_test_client() {
}

