/*
           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                   Version 2, December 2004

	Copyright (C) 2004 Sam Hocevar <sam@hocevar.net>

	Everyone is permitted to copy and distribute verbatim or modified
	copies of this license document, and changing it is allowed as long
	as the name is changed.

			   DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
	  TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

	 0. You just DO WHAT THE FUCK YOU WANT TO.
*/
#include "fmbox.h"

#include <plat.h>
#include <stdlib.h>

uv_loop_t* uv_loop;

client_get_and_decode_jpeg_t jpeg;
client_xiami_index_json_html_t index;

static void jpeg_cb(client_get_and_decode_jpeg_t *cc) {
	int i;
	pixbuf_t buf = cc->buf;

	for (i = 0; i < buf.h; i++)
		memcpy(oled_pix + i*OLED_W*3, buf.data + i*buf.w*3, buf.w*3);
	printf("[jpeg] updated\n");
}

static void index_cb(client_xiami_index_json_html_t *cc) {
	album_t *a = (album_t *)cc->albums.data[0];
	printf("url %s\n", a->pic);

	jpeg.url = a->pic;
	jpeg.cb = jpeg_cb;
	client_start_get_and_decode_jpeg(&jpeg);
}

static void uv_main_thread(void *_) {
	uv_loop = uv_default_loop();

	index.cb = index_cb;
	index.type = XIAMI_INDEX_JSON_HTML_COLLECTS;
	client_start_xiami_index_json_html(&index);

	uv_run(uv_loop, UV_RUN_DEFAULT);
}

int main(int argc, char *argv[]) {

	int has_ui = 1;

	if (has_ui) {
		uv_thread_t id;
		uv_thread_create(&id, uv_main_thread, NULL);
		ui_glut_main(argc, argv);
	} else {
		uv_main_thread(NULL);
	}
}

