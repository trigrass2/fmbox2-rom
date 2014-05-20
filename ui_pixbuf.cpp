#include "fmbox.h"

static void on_data_done(client_t *client) {
	client_get_and_decode_jpeg_t *cc = (client_get_and_decode_jpeg_t *)client->data;

	rewind(client->fp);

	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	
	int row_stride;

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, client->fp);
	cinfo.err = jpeg_std_error(&jerr);

	int r = jpeg_read_header(&cinfo, TRUE);
	if (r != JPEG_HEADER_OK) 
		goto err_out;
	jpeg_start_decompress(&cinfo);

	if (cinfo.output_components != 3)
		goto err_out;

	row_stride = cinfo.output_width * 3;
	cc->buf.data = (char *)malloc(cinfo.output_height * cinfo.output_width * 3);
	cc->buf.w = cinfo.output_width;
	cc->buf.h = cinfo.output_height;

	JSAMPROW row_pointer[1];
	while (cinfo.output_scanline < cinfo.output_height) {
		uint8_t *data = (uint8_t *)(cc->buf.data + cinfo.output_width * 3 * cinfo.output_scanline);
		row_pointer[0] = (JSAMPROW)data;
		jpeg_read_scanlines(&cinfo, row_pointer, 1);
		for (int i = 0; i < cinfo.output_width * 3; i++)
			data[i] &= 0xfc;
	}
	jpeg_finish_decompress(&cinfo);

	if (cc->cb)
		cc->cb(cc);

err_out:
	jpeg_destroy_decompress(&cinfo);	
	fclose(client->fp);
}

void client_start_get_and_decode_jpeg(client_get_and_decode_jpeg_t *cc) {
	client_t *client = &cc->client;
	client_init(client);

	client->data = cc;
	client->fp = tmpfile();
	client->on_data_done = on_data_done;

	client->verbose = 1;
	client->url = cc->url;
	client_start(client);
}

