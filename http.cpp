
#include "fmbox2.h"

enum {
	CLIENT_CHUNKED_READ_LEN_CR = 1,
	CLIENT_CHUNKED_READ_LEN_LF,
	CLIENT_CHUNKED_READ_DATA_CR,
	CLIENT_CHUNKED_READ_DATA_LF,
	CLIENT_CHUNKED_READ_END,
};

static void client_logf(http_client_t *c, int level, const char *fmt, ...) {
	if (level > c->verbose)
		return ;
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fprintf(stderr, "[%d] %s\n", c->_no, buf);
}

static void http_client_free(http_client_t *c) {
	if (c->_url_host)
		free(c->_url_host);
	if (c->_url_path)
		free(c->_url_path);
	if (c->req_content_pb)
		printbuf_free(c->req_content_pb);
	if (c->_text)
		free(c->_text);

	if (c->on_free)
		c->on_free(c);

	free(c);
}

static void on_close(uv_handle_t* handle) {
	http_client_t *c = (http_client_t *) handle->data;

	client_logf(c, 1, "connection closed");
	if (c->on_close)
		c->on_close(c);

	http_client_free(c);
}

static void parse_url(http_client_t *c, char *url) {
	struct http_parser_url u = {};
		
	if (http_parser_parse_url(url, strlen(url), 0, &u))
		return ;
	if (u.field_set & (1<<UF_HOST)) {
		c->_url_host = strndup(
			url + u.field_data[UF_HOST].off,
			u.field_data[UF_HOST].len
		);
	}
	if (u.field_set & (1<<UF_PATH)) {
		c->_url_path = strdup(url + u.field_data[UF_PATH].off);
	}
	if (u.field_set & (1<<UF_PORT)) {
		c->_url_port = strndup(
			url + u.field_data[UF_PORT].off,
			u.field_data[UF_PORT].len
			);
	}
}

static void _on_data(http_client_t* c, const char *buf, size_t len) {
	client_logf(c, 1, "on_data: len %d", len);

	if (c->on_text_done) {
		c->_text = str_append_n(c->_text, (char *)buf, len);
	}

	if (c->on_data)
		c->on_data(c, (void *)buf, len);
	//if (c->fp)
	//		fwrite(buf, 1, len, client->fp);
	//if (client->json_parser)
	//	client->json_obj = json_tokener_parse_ex(client->json_parser, buf, len);
}

static void on_data(uv_stream_t* tcp, const char *buf, size_t len) {
	http_client_t* c = (http_client_t *)tcp->data;

	if (!c->_chunked_state) {
		_on_data(c, buf, len);
		return ;
	}

	// handle chunked data

	int data_start = -1;
	int data_len = -1;

	for (int i = 0; i < (int)len; i++) {
		char ch = buf[i];
		int v;

		switch (c->_chunked_state) {
		case CLIENT_CHUNKED_READ_LEN_CR:
			v = char_hex2dec(ch);
			if (v == -1) {
				if (c->_chunked_len == 0)
					c->_chunked_state = CLIENT_CHUNKED_READ_END;
				else if (ch == '\r')
					c->_chunked_state = CLIENT_CHUNKED_READ_LEN_LF;
			} else {
				c->_chunked_len <<= 4;
				c->_chunked_len += v;
			}
			break;
		case CLIENT_CHUNKED_READ_LEN_LF:
			if (ch == '\n')
				c->_chunked_state = CLIENT_CHUNKED_READ_DATA_CR;
			break;
		case CLIENT_CHUNKED_READ_DATA_CR:
			if (c->_chunked_len == 0) {
				if (ch == '\r')
					c->_chunked_state = CLIENT_CHUNKED_READ_DATA_LF;
				if (data_start != -1) {
					_on_data(c, buf + data_start, data_len);
					data_start = -1;
					data_len = -1;
				}
			} else {
				c->_chunked_len--;
				if (data_start == -1) {
					data_start = i;
					data_len = 0;
				}
				data_len++;
			}
			break;
		case CLIENT_CHUNKED_READ_DATA_LF:
			if (ch == '\n') {
				c->_chunked_state = CLIENT_CHUNKED_READ_LEN_CR;
				c->_chunked_len = 0;
			}
			break;
		}
	}

	if (data_start != -1)
		_on_data(c, buf + data_start, data_len);

	//LOGF("[%d] on_data: len %d", client->no, len);
}

static void on_data_done(uv_stream_t *tcp) {
	http_client_t *c = (http_client_t *)tcp->data;

	client_logf(c, 1, "on_data_done");

	if (c->on_data_done)
		c->on_data_done(c);

	if (c->on_text_done)
		c->on_text_done(c, c->_text);
}

static int on_headers_complete(http_parser* parser) {
	http_client_t *c = (http_client_t *)parser->data;

	client_logf(c, 1, "on_headers_complete");

	return 1;
}

static int on_header_field(http_parser *p, const char *buf, size_t len) {
	http_client_t *c = (http_client_t *)p->data;

	char *s = (char *)malloc(len+1);
	memcpy(s, buf, len);
	s[len] = 0;
	c->_header_field = s;

	return 0;
}


static int on_header_value(http_parser *p, const char *buf, size_t len) {
	http_client_t *c = (http_client_t *)p->data;

	char *value = (char *)malloc(len+1);
	memcpy(value, buf, len);
	value[len] = 0;

	client_logf(c, 1, "on_header_value: %s: %s", c->_header_field, value);

	int i;
	for (i = 0;
		i < sizeof(c->resp_headers_save)/sizeof(c->resp_headers_save[0]) && 
		c->resp_headers_save[i]
		; i++) {
		if (!strcmp(c->resp_headers_save[i], c->_header_field)) {
			c->resp_headers_val[i] = str_append(c->resp_headers_val[i], value);
		}
	}

	if (!strcmp(c->_header_field, "Transfer-Encoding") && !strcmp(value, "chunked")) {
		c->_chunked_state = CLIENT_CHUNKED_READ_LEN_CR;
	}

	if (c->_header_field) {
		free(c->_header_field);
		c->_header_field = NULL;
	}

	free(value);

	return 0;
}


static void on_alloc(uv_handle_t* client, size_t suggested_size, uv_buf_t *buf) {
	buf->base = (char *)malloc(suggested_size);
	buf->len = suggested_size;
}

static void on_read(uv_stream_t* tcp, ssize_t nread, const uv_buf_t *buf) {
	http_client_t* c = (http_client_t *)tcp->data;
	ssize_t parsed;

	client_logf(c, 1, "on_read: len %d", nread);

	if (nread >= 0) {
		c->_parser_settings.on_header_field = on_header_field;
		c->_parser_settings.on_header_value = on_header_value;
		c->_parser_settings.on_headers_complete = on_headers_complete;

		parsed = http_parser_execute(
			&c->_parser, &c->_parser_settings, buf->base, nread);
		if (parsed < nread && parsed != 0) {
			//client_logf(c, 1, "parse error: %s", http_errno_description(HTTP_PARSER_ERRNO(&c->_parser)));
		}
		on_data(tcp, buf->base + parsed, nread - parsed);
	} else {
		on_data_done(tcp);
		uv_close((uv_handle_t *)tcp, on_close);
	}

	free(buf->base);
}

static void after_write(uv_write_t* req, int status) {
	http_client_t *c = (http_client_t *)req->data;

	if (status) {
		client_logf(c, 1, "write failed");
		http_client_free(c);
		return ;
	}

	http_parser_init(&c->_parser, HTTP_RESPONSE);
	c->_parser.data = c;

	uv_read_start((uv_stream_t *)&c->_socket, on_alloc, on_read);
}

static void on_connect(uv_connect_t *stream, int status) {
	http_client_t *c = (http_client_t *)stream->data;
	int r;

	if (status) {
		client_logf(c, 1, "connect failed: %d", status);
		http_client_free(c);
		return ;
	}

	client_logf(c, 1, "connect ok");

	c->_write_req.handle = (uv_stream_t *)stream;
	c->_write_req.data = c;
	
	if (c->_url_path == NULL) {
		client_logf(c, 1, "url path invalid");
		http_client_free(c);
		return ;
	}

	const char *ua = c->user_agent;
	if (!ua)
		ua = "Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6)";

	int buf_nr = 1;
	int clen = 0;
	
	if (c->req_content_pb) {
		buf_nr = 2;
		c->_uvbuf[1].base = c->req_content_pb->buf;	
		c->_uvbuf[1].len = printbuf_length(c->req_content_pb);
		clen = c->_uvbuf[1].len;
		client_logf(c, 1, "req content len: %d", clen);
	}

	c->_pb[0] = printbuf_new();
	sprintbuf(c->_pb[0], 
		"GET %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"Accept: */*\r\n"
		"Connection: close\r\n"
		"Content-Length: %d\r\n"
		"User-Agent: %s\r\n"
		"\r\n",
		c->_url_path, c->_url_host, clen, ua);
	c->_uvbuf[0].base = c->_pb[0]->buf;
	c->_uvbuf[0].len = printbuf_length(c->_pb[0]);

	r = uv_write(&c->_write_req, stream->handle, c->_uvbuf, buf_nr, after_write);
	if (r) {
	  client_logf(c, 1, "write failed: %d", r);
		http_client_free(c);
	}
}

static void on_resolved(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res) {
	http_client_t *c = (http_client_t *)resolver->data;

	if (!status) {
		char addr[17] = {'\0'};
		uv_ip4_name((struct sockaddr_in *) res->ai_addr, addr, 16);
		client_logf(c, 1, "resolved: %s", addr);

		uv_tcp_init(uv_loop, &c->_socket);
		c->_connect_req.data = c;
		c->_socket.data = c;

		uv_tcp_connect(
			&c->_connect_req,
			(uv_tcp_t *)&c->_socket,
			(const struct sockaddr *)res->ai_addr, on_connect);
	} else {
		client_logf(c, 1, "getaddrinfo callback error: %d", status);
		http_client_free(c);
		return;
	}

	uv_freeaddrinfo(res);
}

static int client_no;

void http_client_do(http_client_t *c) {
	c->_no = client_no++;
	c->_resolver.data = c;

	if (c->url)
		parse_url(c, c->url);
	if (c->url_pb) 
		parse_url(c, c->url_pb->buf);
	if (c->url_host) 
		c->_url_host = strdup(c->url_host);
	if (c->_url_host == NULL) {
		client_logf(c, 1, "url invalid or host not set");
		return ;
	}

	char *port = c->_url_port;
	if (port == NULL) 
		port = (char *)"80";

	client_logf(c, 1, "resolve %s:%s", c->_url_host, port);
	uv_getaddrinfo(uv_loop, &c->_resolver, on_resolved, c->_url_host, port, NULL);
}

http_client_t *http_client_new() {
	return (http_client_t *)zalloc(sizeof(http_client_t));
}

/*
 * === Unit Tests ===
 */

static void test_simple_xml_done(http_client_t *c, xml_t *x) {
	test_t *t = (test_t *)c->data;
}

static void do_test_simple_xml(test_t *t) {
	http_client_t *c = http_client_new();
	c->data = t;
	c->url = (char *)"http://127.0.0.1:1989/echo";

	c->req_content_pb = printbuf_new();
	sprintbuf(c->req_content_pb, "\
		<div>\
			<ul>\
				<li>\
					<a class='aa' title='xx'>abc</a>\
				</li>\
			</ul>\
		</div>\
	");

	c->on_xml_done = test_simple_xml_done;
	http_client_do(c);
}

static void test_save_cookie_done(http_client_t *c, char *text) {
	test_t *t = (test_t *)c->data;

	if (!strcmp(c->resp_headers_val[0], "k3=v3; k4=v4;")) {
		fprintf(stderr, "[OK] save cookie\n");
		test_next(t);
	} else {
		fprintf(stderr, "[FAIL] save cookie\n");
		test_fail(t);
	}
}

static void do_test_save_cookie(test_t *t) {
	http_client_t *c = http_client_new();
	c->data = t;
	c->url = (char *)"http://127.0.0.1:1989/echo";
	c->resp_headers_save[0] = "Set-Cookie";
	c->on_text_done = test_save_cookie_done;
	http_client_do(c);
}

static void test_simple_echo_done(http_client_t *c, char *text) {
	test_t *t = (test_t *)c->data;

	client_logf(c, 1, "text: %s", text);

	if (!strcmp(text, "hello,world")) {
		fprintf(stderr, "[OK] simple echo\n");
		test_next(t);
	} else {
		fprintf(stderr, "[FAIL] simple echo\n");
		test_fail(t);
	}
}

static void do_test_simple_echo(test_t *t) {
	http_client_t *c = http_client_new();
	c->data = t;
	c->url = (char *)"http://127.0.0.1:1989/echo";

	c->req_content_pb = printbuf_new();
	sprintbuf(c->req_content_pb, "hello,world");

	c->on_text_done = test_simple_echo_done;
	http_client_do(c);
}

void test_http_client(test_t *t) {
	t = test_new(t);
	t->tests[t->nr++] = do_test_simple_echo;
	t->tests[t->nr++] = do_test_save_cookie;
	t->tests[t->nr++] = do_test_simple_xml;
	test_next(t);
}

