#include <stdio.h>
#include <string.h>

#include "fmbox.h"

char *strdup_xmlContent(xmlNode *node) {
	char *s;
	xmlChar *xs = xmlNodeGetContent(node);
	s = strdup((char *)xs);
	xmlFree(xs);
	return s;
}

static char *conv_xiami_location(char *in) {
	int len = strlen(in);

	if (in == NULL)
		return NULL;
	if (len < 2) {
		free(in);
		return NULL;
	}

	int num = in[0] - '0';
	int avg_len = (len-1)/num;
	int reminder = (len-1)%num;
	int i, j, k;
	char *out;

	out = (char *)malloc(len*2);
	char *outp = out;

	for (i = 0; i < avg_len; i++) {
		for (j = 0; j < reminder; j++)
			*outp++ = in[ j*(avg_len+1)+1 + i ];
		for (j = 0; j < num-reminder; j++)
			*outp++ = in[ reminder*(avg_len+1) + j*avg_len+1 + i ];
	}
	for (j = 0; j < reminder; j++)
		*outp++ = in[ j*(avg_len+1)+1 + avg_len ];

	j = 0;
	i = 0;
	while (i < outp-out) {
		if (out[i] == '%' && i+1 < outp-out && out[i+1] == '%') {
			out[j] = '%';
			j++;
			i += 2;
		} else if (out[i] == '%' && i+2 < outp-out) {
			out[j] = (char_hex2dec(out[i+1])<<4) + char_hex2dec(out[i+2]);
			j++;
			i += 3;
		} else {
			out[j] = out[i];
			i++;
			j++;
		}
		if (out[j-1] == '^')
			out[j-1] = '0';
	}
	out[j] = 0;
	free(in);

	return out;
}

static void walk_element(xmlNode *a_node, arr_t *songs, song_t *s) {
    xmlNode *cur_node = NULL;

    for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
        if (cur_node->type == XML_ELEMENT_NODE) {

			//printf("node type: Element, name: %s\n", cur_node->name);

			if (!strcmp((char *)cur_node->name, "track")) {
				s = (song_t *)malloc(sizeof(song_t));
				memset(s, 0, sizeof(*s));
				arr_append(songs, s);
				walk_element(cur_node->children, songs, s);
			} else 
				walk_element(cur_node->children, songs, NULL);

			if (s && !strcmp((char *)cur_node->name, "title"))
				s->title = strdup_xmlContent(cur_node);
			if (s && !strcmp((char *)cur_node->name, "album_name"))
				s->album = strdup_xmlContent(cur_node);
			if (s && !strcmp((char *)cur_node->name, "pic"))
				s->pic = strdup_xmlContent(cur_node);
			if (s && !strcmp((char *)cur_node->name, "artist"))
				s->artist = strdup_xmlContent(cur_node);
			if (s && !strcmp((char *)cur_node->name, "location"))
				s->url = conv_xiami_location(strdup_xmlContent(cur_node));
        } else
			walk_element(cur_node->children, songs, NULL);
    }
}

static void on_data_done(client_t *client) {
	client_xiami_playlist_xml_t *cc = (client_xiami_playlist_xml_t *)client->data;

	xmlParseChunk(cc->ctxt, NULL, 0, 1);
	if (cc->ctxt->myDoc) {
		song_t *list = NULL;
		xmlNode *root_element = xmlDocGetRootElement(cc->ctxt->myDoc);
		walk_element(root_element, &cc->songs, NULL);
		if (cc->cb)
			cc->cb(cc);
	}
	xmlFreeDoc(cc->ctxt->myDoc);
	xmlFreeParserCtxt(cc->ctxt);
}

static void on_data(client_t *client, const char *buf, size_t len) {
	client_xiami_playlist_xml_t *cc = (client_xiami_playlist_xml_t *)client->data;

	xmlParseChunk(cc->ctxt, buf, len, 0);
}

void client_start_xiami_playlist_xml(client_xiami_playlist_xml_t *cc) {

	client_t *client = &cc->client;
	memset(&cc->songs, 0, sizeof(cc->songs));
	client_init(client);
	client->data = cc;

	cc->ctxt = xmlCreatePushParserCtxt(NULL, NULL, NULL, 0, NULL);
	xmlCtxtUseOptions(cc->ctxt, XML_PARSE_RECOVER|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);

	client->pb = printbuf_new();
	client->host = "www.xiami.com";

	client->on_data_done = on_data_done;
	client->on_data = on_data;

	switch (cc->type) {
	case XIAMI_PLAYLIST_XML_RADIO_USER:
		// http://www.xiami.com/radio/xml/type/4/id/3110397
		//    个人电台, id 为 userid, userid 可以从 Cookie 中的 user= 开始处获得
		sprintbuf(client->pb, "GET /radio/xml/type/4/id/%s HTTP/1.1\r\n", cc->id);
		break;

	case XIAMI_PLAYLIST_XML_RADIO_GUESS:
		sprintbuf(client->pb, "GET /radio/xml/type/8/id/0 HTTP/1.1\r\n"); // 虾米猜电台	
		break;

	case XIAMI_PLAYLIST_XML_ALBUM_TODAY_RECOMMEND:
		sprintbuf(client->pb, "GET /song/playlist/id/1/type/9 HTTP/1.1\r\n"); // 今日推荐专辑
		break;

	case XIAMI_PLAYLIST_XML_COLLECT:
		// /song/playlist/id/29908395/type/3 精选集  播放列表
		sprintbuf(client->pb, "GET /song/playlist/id/%s/type/3 HTTP/1.1\r\n", cc->id);
		break;

	case XIAMI_PLAYLIST_XML_ALBUM:
		// /song/playlist/id/29908395/type/1 普通专辑播放列表
		sprintbuf(client->pb, "GET /song/playlist/id/%s/type/1 HTTP/1.1\r\n", cc->id); 
		break;	
	}

	sprintbuf(client->pb, 
		"Host: www.xiami.com\r\n"
		"User-Agent: Mozilla/5.0 (Windows; U; Windows NT 5.1; en-US; rv:1.7.6)\r\n"
		);
	if (client->cookies != NULL)
		sprintbuf(client->pb, "Cookies: %s\r\n", client->cookies);
	sprintbuf(client->pb, "\r\n");
	client->uvbuf[0].base = client->pb->buf;
	client->uvbuf[0].len = printbuf_length(client->pb);
	client->uvbuf_nr = 1;
}

void test_parse_xiami_playlist_xml() {
	xmlDoc *doc = NULL;
    xmlNode *root_element = NULL;
    doc = xmlReadFile("a.xml", "utf-8", XML_PARSE_RECOVER|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
    if (doc == NULL) {
        printf("error: could not parse file\n");
		return ;
    }
    root_element = xmlDocGetRootElement(doc);

	arr_t songs = {};
    walk_element(root_element, &songs, NULL);

	for (int i = 0; i < songs.len; i++) {
		song_t *s = (song_t *)songs.data[i];
		printf("song: %s\n", s->url);
	}

    xmlFreeDoc(doc);
    xmlCleanupParser();

	return ;
}
