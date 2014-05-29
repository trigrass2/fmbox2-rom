#pragma once

#include "xml.h"
#include "utils.h"

typedef struct _http_client_t {
	// private data
	void *data;

	// verbose debug log
	int verbose;

	// send request content buffer
	struct printbuf *req_content_pb;

	// send request content file
	FILE *req_content_fp;

	// send request content form
	json_object *req_content_form;

	// set User-Agent
	const char *user_agent;

	// set request headers
	json_object *req_headers;

	// save some response header
	const char *resp_headers_save[16];
	char *resp_headers_val[16];

	// save response content to file
	FILE *resp_content_fp;

	// set request url
	char *url; 
	struct printbuf *url_pb;
	char *url_host;

	// content is json
	void (*on_json_done)(struct _http_client_t *, json_object *obj);

	// content is xml or html
	void (*on_xml_done)(struct _http_client_t *, xml_t *xml);

	// content is text
	void (*on_text_done)(struct _http_client_t *, char *text);

	// download process
	void (*on_process)(struct _http_client_t *, int cur, int total);

	// on data recv
	void (*on_data)(struct _http_client_t *, void *buf, int len);

	// on data done
	void (*on_data_done)(struct _http_client_t *);

	// on close
	void (*on_close)(struct _http_client_t *);

	// on free
	void (*on_free)(struct _http_client_t *);

	int _no;
	char *_url_host;
	char *_url_path;
	char *_url_port;
	struct printbuf *_pb[2];
	uv_buf_t _uvbuf[2];
	uv_getaddrinfo_t _resolver;
	uv_connect_t _connect_req;
	uv_tcp_t _socket;
	uv_write_t _write_req;
	http_parser _parser;
	http_parser_settings _parser_settings;
	char *_header_field;
	int _chunked_state;
	int _chunked_len;
	char *_text;

} http_client_t;

void http_client_do(http_client_t *req);

void test_http_client(test_t *);


