#pragma once

#include <time.h>
#include <stdio.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <jpeglib.h>
#include <uv.h>
#include "http_parser.h"
#include <json-c/json.h>
#include <json-c/printbuf.h>

#include <plat.h>

extern uv_loop_t* uv_loop;

#define LOGF(fmt, ...) printf(fmt "\n", __VA_ARGS__);

int char_hex2dec(char i);
int str_startswith(char *s, char *with);
char *str_dup_n(char *s, int n);
char *str_pick_inner(char *s, char *start, char *end, char **pos);
char *str_append(char *s, char *s2);
int str_cut(char *s, char cut);

typedef struct {
	void **data;
	int len, _size;
} arr_t; // zero to init
void arr_append(arr_t *arr, void *data);
void arr_free(arr_t *arr);

json_object *json_from_file(char *filename);

enum {
	CLIENT_CHUNKED_READ_LEN_CR = 1,
	CLIENT_CHUNKED_READ_LEN_LF,
	CLIENT_CHUNKED_READ_DATA_CR,
	CLIENT_CHUNKED_READ_DATA_LF,
	CLIENT_CHUNKED_READ_END,
};

typedef struct _client_t {
	int no;
	int verbose;

	void *data;
	void *data2;

	char *host, *_host;

	uv_getaddrinfo_t resolver;
	uv_connect_t connect_req;
	uv_tcp_t socket;
	uv_write_t write_req;
	http_parser parser;
	http_parser_settings parser_settings;
	
	FILE *fp; // when set do saving content to file

	json_tokener *json_parser; // when set do parsing json
	json_object *json_obj;

	int save_cookie; // when set save cookies
	char *cookies;

	struct printbuf *pb, *pb2;

	// set request content
	struct printbuf *req_content_pb;
	FILE *req_content_fp;

	// set request header
	struct printbuf *req_headers_pb[16];
	const char *req_headers_val[16];
	const char *req_headers_name[16];

	// get response header
	const char *resp_headers_name[16];
	char *resp_headers_val[16];

	// get response content
	FILE *resp_content_fp;

	// set request url
	char *url; 
	struct printbuf *url_pb;

	uv_buf_t uvbuf[2]; 	// http request data
	int uvbuf_nr;

	char *_header_field;
	int _chunked_state;
	int _chunked_len;

	void (*on_json_done)(struct _client_t *, json_object *obj);
	void (*on_html_done)(struct _client_t *, xml_t *xml);
	void (*on_text_done)(struct _client_t *, char *text);

	void (*on_data_done)(struct _client_t *);
	void (*on_data)(struct _client_t *, const char *buf, size_t len);
} client_t;

typedef void (*client_cb_t)(client_t *);

static inline void CLIENT_LOGF(client_t *c, const char *fmt, ...) {
	if (!c->verbose)
		return ;

	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fprintf(stderr, "[%d] %s\n", c->no, buf);
}

void client_init(client_t *client);
void client_start(client_t *client);

typedef struct _song_t {
	char *url;
	char *pic;
	char *artist;
	char *title;
	char *album;
} song_t;

void song_free(song_t *s);

typedef struct _album_t {
	void *data;
	char *pic;
	char *title;
	char *href;
	char *album_id;
	char *artist;
	char *artist_id;
} album_t;

void album_free(album_t *a);

typedef struct _client_xiami_login_t {
	char *email; // in
	char *pass; // in
	void (*cb)(_client_xiami_login_t *); // in
	char *user_id; // out
	client_t client; // client.cookies out
} client_xiami_login_t;
void client_start_xiami_login(client_xiami_login_t *);

enum {
	XIAMI_PLAYLIST_XML_RADIO_USER, // 个人电台
	XIAMI_PLAYLIST_XML_RADIO_GUESS, // 虾米猜电台
	XIAMI_PLAYLIST_XML_ALBUM_TODAY_RECOMMEND, // 今日推荐专辑
	XIAMI_PLAYLIST_XML_COLLECT, // 精选集
	XIAMI_PLAYLIST_XML_ALBUM, // 普通专辑
};
typedef struct _client_xiami_playlist_xml_t {
	int type; // in
	char *id; // in
	void (*cb)(_client_xiami_playlist_xml_t *); // in
	char *cookies; // in
	arr_t songs; // out
	xmlParserCtxtPtr ctxt;
	client_t client;
} client_xiami_playlist_xml_t;
void client_start_xiami_playlist_xml(client_xiami_playlist_xml_t *);

void test_parse_xiami_playlist_xml();

enum {
	XIAMI_INDEX_JSON_HTML_GUESS_YOU_LIKE_ALBUMS, // 首页.猜你喜欢
	XIAMI_INDEX_JSON_HTML_COLLECTS, // 首页.精选集
};
typedef struct _client_xiami_index_json_html_t {
	int type; // in
	void (*cb)(_client_xiami_index_json_html_t *); // in
	char *cookies; // in
	arr_t albums; // out
	client_t client;
} client_xiami_index_json_html_t ;
void client_start_xiami_index_json_html(client_xiami_index_json_html_t *);

void test_parse_xiami_index_json_html();

void ui_glut_main(int argc, char **argv);

typedef struct {
	char *data;
	int w, h;
} pixbuf_t;

#define OLED_W 320
#define OLED_H 240

extern uint8_t oled_pix[];

typedef struct _client_get_and_decode_jpeg_t {
	char *url; // in
	pixbuf_t buf; // out
	client_t client;
	void (*cb)(_client_get_and_decode_jpeg_t*);
} client_get_and_decode_jpeg_t;
void client_start_get_and_decode_jpeg(client_get_and_decode_jpeg_t *cc);

