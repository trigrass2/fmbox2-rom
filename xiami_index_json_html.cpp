
#include "fmbox.h"

static FILE *dump_fp;

typedef struct _selector_t {
	void *data;

	const char *tag;
	const char *class_;
	const char *has_attr[4]; 
	const char *attr_startswith[4];
	char *attr[4];
	struct _selector_t *child[4];
	void (*cb)(_selector_t *, xmlNode *);
} selector_t;

void album_free(album_t *a) {
	if (a->pic)
		free(a->pic);
	if (a->title)
		free(a->title);
	if (a->href)
		free(a->href);
	if (a->album_id)
		free(a->album_id);
	if (a->artist)
		free(a->artist);
	if (a->artist_id)
		free(a->artist_id);
}

static char *xmlNodeGetAttr(xmlNode *n, const char *name) {
	xmlChar *s = xmlGetProp(n, (const xmlChar *)name);
	char *r = strdup((const char *)s);
	xmlFree(s);
	return r;
}

static int selector_match(selector_t *sel, xmlNode *n) {
	if (strcmp((char *)n->name, sel->tag))
		return 0;
	if (sel->class_) {
		xmlChar *class_ = ::xmlGetProp(n, (const xmlChar *)"class");
		if (class_ == NULL)
			return 0;
		if (strcmp((const char *)class_, sel->class_)) {
			xmlFree(class_);
			return 0;
		}
		xmlFree(class_);
	}
	for (int i = 0; sel->has_attr[i]; i++) {
		if (!xmlHasProp(n, (const xmlChar *)sel->has_attr[i]))
			return 0;

		char *prefix = (char *)sel->attr_startswith[i];
		if (prefix) {
			char *value = xmlNodeGetAttr(n, sel->has_attr[i]);
			if (!str_startswith(value, prefix)) {
				free(value);
				return 0;
			}
			free(value);
		}
	}
	return 1;
}

static void walk_element(FILE *dump_fp, selector_t *sel, xmlNode *a_node, int depth) {
    xmlNode *cur_node = NULL;

    for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE) {

			if (dump_fp) {
				for (int i = 0; i < depth; i++)
					fprintf(dump_fp, " ");
				fprintf(dump_fp, "%s\r\n", cur_node->name);
			}
		
			selector_t *child = NULL;

			for (int i = 0; sel->child[i]; i++) {
				if (selector_match(sel->child[i], cur_node)) {
					child = sel->child[i];
				}
			}

			if (child && child->cb)
				child->cb(child, cur_node);

			walk_element(dump_fp, child ? child : sel, cur_node->children, depth+1);
		}
	}
}

static void sel_cb_album_new(selector_t *sel, xmlNode *n) {
	arr_t *albums = (arr_t *)sel->data;

	album_t *p = (album_t *)malloc(sizeof(album_t));
	memset(p, 0, sizeof(album_t));
	arr_append(albums, p);

	xmlBuffer *buf = xmlBufferCreate();
	xmlNodeDump(buf, n->doc, n, 5, 1);

	char *pos;
	p->album_id = str_pick_inner((char *)buf->content, "href=\"/album/", "\"", &pos);
	if (p->album_id)
		p->title = str_pick_inner(pos, "title=\"", "\"", &pos);
	
	p->artist_id = str_pick_inner(pos, "href=\"/artist/", "\"", &pos);
	if (p->artist_id)
		p->artist = str_pick_inner(pos, "title=\"", "\"", &pos);

	xmlBufferFree(buf);
}

static void sel_cb_album_img_src(selector_t *sel, xmlNode *n) {
	arr_t *albums = (arr_t *)sel->data;
	if (albums->len == 0)
		return ;

	album_t *p = (album_t *)albums->data[albums->len-1];
	p->pic = xmlNodeGetAttr(n, "src");
}

static void sel_cb_collect_href_title(selector_t *sel, xmlNode *n) {
	arr_t *albums = (arr_t *)sel->data;
	if (albums->len == 0)
		return ;
	album_t *p = (album_t *)albums->data[albums->len-1];

	p->title = xmlNodeGetAttr(n, "title");
	p->href = xmlNodeGetAttr(n, "href");

	char *album_id = strstr(p->href, "id/");
	if (album_id)
		p->album_id = strdup(album_id+3);
}

static void sel_cb_album_href_title(selector_t *sel, xmlNode *n) {
	arr_t *albums = (arr_t *)sel->data;
	if (albums->len == 0)
		return ;
	album_t *p = (album_t *)albums->data[albums->len-1];

	p->title = xmlNodeGetAttr(n, "title");
	p->href = xmlNodeGetAttr(n, "href");
}

static const char *json_to_html(json_object *obj, int type) {
	const char *html = NULL;

	json_object *obj_data = json_object_object_get(obj, "data");
	if (obj_data == NULL) {
		return html;
	}

	if (type == XIAMI_INDEX_JSON_HTML_GUESS_YOU_LIKE_ALBUMS) {
		html = json_object_get_string(obj_data);
	}
	if (type == XIAMI_INDEX_JSON_HTML_COLLECTS) {
		json_object *obj_collects = json_object_object_get(obj_data, "collects");
		if (obj_collects != NULL) {
			html = json_object_get_string(obj_collects);
		}
	}

	return html;
}

static void parse_all(arr_t *albums, json_object *obj, int type, char *dump_html_to_file, char *dump_xml_tree_to_file) {
	const char *html = json_to_html(obj, type);
	if (html == NULL)
		return ;

	if (dump_html_to_file) {
		FILE *fp = fopen(dump_html_to_file, "wb+");
		fwrite(html, 1, strlen(html), fp);
		fclose(fp);
	}

	FILE *dump_fp = NULL;
	if (dump_xml_tree_to_file) 
		dump_fp = fopen(dump_xml_tree_to_file, "wb+");

	xmlParserCtxtPtr ctxt;
	ctxt = xmlCreatePushParserCtxt(NULL, NULL, NULL, 0, NULL);
	xmlCtxtUseOptions(ctxt, XML_PARSE_RECOVER);
	xmlParseChunk(ctxt, "<div>", 5, 0);
	xmlParseChunk(ctxt, html, strlen(html), 0);
	xmlParseChunk(ctxt, "</div>", 6, 1);

	selector_t sel_root = {}, sel[8] = {};

	if (type == XIAMI_INDEX_JSON_HTML_COLLECTS) {
		sel[0].tag = "div"; sel[0].class_ = "collect";
		sel[0].child[0] = &sel[1];
		sel[0].cb = sel_cb_album_new;
		sel[0].data = albums;
			sel[1].tag = "div"; sel[1].class_ = "image";
			sel[1].child[0] = &sel[2];
				sel[2].tag = "img";
				sel[2].has_attr[0] = "src";
				sel[2].cb = sel_cb_album_img_src;
				sel[2].data = albums;
		sel[0].child[1] = &sel[3];
			sel[3].tag = "div"; sel[3].class_ = "info";
			sel[3].child[0] = &sel[4];
				sel[4].tag = "p"; sel[4].class_ = "name";
				sel[4].child[0] = &sel[5];
					sel[5].tag = "a";
					sel[5].has_attr[0] = "href";
					sel[5].has_attr[1] = "title";
					sel[5].cb = sel_cb_collect_href_title;
					sel[5].data = albums;
		sel_root.child[0] = &sel[0];
	} else {
		sel[0].tag = "div"; sel[0].class_ = "album";
		sel[0].child[0] = &sel[1];
		sel[0].cb = sel_cb_album_new;
		sel[0].data = albums;
			sel[1].tag = "div"; sel[1].class_ = "image";
			sel[1].child[0] = &sel[2];
				sel[2].tag = "img";
				sel[2].has_attr[0] = "src";
				sel[2].cb = sel_cb_album_img_src;
				sel[2].data = albums;
		sel_root.child[0] = &sel[0];
	}

	walk_element(dump_fp, &sel_root, xmlDocGetRootElement(ctxt->myDoc), 0);

	if (dump_fp)
		fclose(dump_fp);

	xmlFreeDoc(ctxt->myDoc);
	xmlFreeParserCtxt(ctxt);
	xmlCleanupParser();	
}

static void on_data_done(client_t *client) {
	client_xiami_index_json_html_t *cc = (client_xiami_index_json_html_t *)client->data;

	if (client->json_obj == NULL) 
		return ;

	parse_all(&cc->albums, client->json_obj, cc->type, NULL, NULL);
	if (cc->cb)
		cc->cb(cc);
}

void client_start_xiami_index_json_html(client_xiami_index_json_html_t *cc) {

	client_t *client = &cc->client;
	memset(&cc->albums, 0, sizeof(cc->albums));
	client_init(client);
	client->data = cc;

	client->pb = printbuf_new();
	client->host = "www.xiami.com";

	client->json_parser = json_tokener_new();
	client->on_data_done = on_data_done;

	switch (cc->type) {
	case XIAMI_INDEX_JSON_HTML_GUESS_YOU_LIKE_ALBUMS:
		sprintbuf(client->pb, "GET /index/recommend HTTP/1.1\r\n");
		break;
	case XIAMI_INDEX_JSON_HTML_COLLECTS:
		sprintbuf(client->pb, "GET /index/collect HTTP/1.1\r\n");
		break;
	}

	sprintbuf(client->pb, 
		"Host: www.xiami.com\r\n"
		"User-Agent: Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6)\r\n"
		);
	if (cc->cookies != NULL)
		sprintbuf(client->pb, "Cookies: %s\r\n", cc->cookies);
	sprintbuf(client->pb, "\r\n");
	client->uvbuf[0].base = client->pb->buf;
	client->uvbuf[0].len = printbuf_length(client->pb);
	client->uvbuf_nr = 1;

	client_start(client);
}

void test_parse_xiami_index_json_html() {
	json_object *obj = json_from_file("xx");
	if (obj == NULL) {
		return;
	}

	arr_t albums = {};
	parse_all(&albums, obj, 0, NULL, NULL);
}
