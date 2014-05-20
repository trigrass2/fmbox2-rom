
#include "fmbox.h"

static void on_data_done(client_t *client) {
	client_xiami_login_t *cc = (client_xiami_login_t *)client->data;

	if (client->cookies == NULL)
		return ;

	if (str_startswith(client->cookies, "user=")) {
		int i;
		for (i = 5; client->cookies[i]; i++)
			if (!(client->cookies[i] >= '0' && client->cookies[i] <= '9')) 
				break;	
		cc->user_id = str_dup_n(client->cookies+5, i-5);
		if (cc->cb)
			cc->cb(cc);
	}
}

void client_start_xiami_login(client_xiami_login_t *cc) {

	client_t *client = &cc->client;
	cc->user_id = NULL;
	client_init(client);
	client->data = cc;

	client->on_data_done = on_data_done;
	client->host = "www.xiami.com";

	client->pb = printbuf_new();
	client->pb2 = printbuf_new();
	sprintbuf(client->pb2, "password=%s&email=%s", cc->pass, cc->email);
	client->uvbuf_nr = 2;
	client->uvbuf[1].base = client->pb2->buf;
	client->uvbuf[1].len = strlen(client->uvbuf[1].base);

	srand((unsigned int)time(NULL));
	double d_rand = rand()/((double)RAND_MAX + 1);

	// 从虾米电台 AIR 应用抓包得出
	// 另外 iOS 客户端采用的是 OAuth2 接口登陆，很难实现
	sprintbuf(client->pb,
		"POST /kuang/login HTTP/1.1\r\n" 
		"Referer: http://www.xiami.com/res/kuang/xiamikuang4air.swf?rnd=%.lf\r\n"
		"Accept: text/xml, application/xml, application/xhtml+xml, text/html;q=0.9, "
			"text/plain;q=0.8, text/css, image/png, image/jpeg, image/gif;q=0.8, "
			"application/x-shockwave-flash, video/mp4;q=0.9, flv-application/octet-stream;q=0.8, "
			"video/x-flv;q=0.7, audio/mp4, application/futuresplash, */*;q=0.5\r\n"
		"x-flash-version: 11,7,700,224\r\n"
		"Content-Type: application/x-www-form-urlencoded\r\n"
		"User-Agent: Mozilla/5.0 (Windows; U; zh-CN) AppleWebKit/533.19.4 (KHTML, like Gecko) AdobeAIR/3.7\r\n"
		"Connection: Keep-Alive\r\n"
		"Content-Length: %d\r\n"
		"Host: www.xiami.com\r\n"
		"\r\n",
		d_rand,
		client->uvbuf[1].len);
	client->uvbuf[0].base = client->pb->buf;
	client->uvbuf[0].len = printbuf_length(client->pb);
	client->save_cookie = 1;
}

