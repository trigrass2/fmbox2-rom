#include "fmbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int char_hex2dec(char i) {
	if (i >= '0' && i <= '9')
		return i - '0';
	else if (i >= 'a' && i <= 'f')
		return i - 'a' + 10;
	else if (i >= 'A' && i <= 'F')
		return i - 'A' + 10;
	else
		return -1;
}

char *str_pick_inner(char *s, char *start, char *end, char **p) {
	s = strstr(s, start);
	if (s == NULL)
		return s;
	s += strlen(start);
	end = strstr(s, end);
	if (end == NULL)
		return NULL;
	if (p)
		*p = end;
	return str_dup_n(s, end - s);
}

char *str_dup_n(char *s, int n) {
	char *r = (char *)malloc(n+1);
	memcpy(r, s, n);
	r[n] = 0;
	return r;
}

int str_cut(char *s, char cut) {
	while (*s) {
		if (*s == cut) {
			*s = 0;
			return 1;
		}
		s++;
	}
	return 0;
}

char *str_append(char *s, char *s2) {
	if (s == NULL)
		return strdup(s2);
	if (s2 == NULL)
		return s;
	int len = strlen(s);
	int len2 = strlen(s2);
	char *s3 = (char *)malloc(len+len2+1);
	memcpy(s3, s, len);
	memcpy(s3+len, s2, len2);
	s3[len+len2] = 0;
	free(s);
	return s3;
}

int str_startswith(char *s, char *with) {
	return !strncmp(s, with, strlen(with));
}

void song_free(song_t *s) {
	if (s->url)
		free(s->url);
	if (s->artist)
		free(s->artist);
	if (s->title)
		free(s->title);
	if (s->pic)
		free(s->pic);
	if (s->album)
		free(s->album);
	free(s);
}

void arr_free(arr_t *arr) {
	free(arr->data);
}

void arr_append(arr_t *arr, void *data) {
	if (arr->data == NULL) {
		arr->_size = 128;
		arr->data = (void **)malloc(sizeof(void*) * arr->_size);
	}
	if (arr->len >= arr->_size) {
		arr->_size *= 2;
		arr->data = (void **)realloc(arr->data, sizeof(void*) * arr->_size);
	}
	arr->data[arr->len] = data;
	arr->len++;
}

json_object *json_from_file(char *filename) {
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL)
		return NULL;

	json_tokener *tok = json_tokener_new();
	json_object *obj = NULL;

	char buf[1024];
	for (;;) {
		int r = fread(buf, 1, sizeof(buf), fp);
		obj = json_tokener_parse_ex(tok, buf, r);
		if (feof(fp))
			break;
	}
	fclose(fp);

	json_tokener_free(tok);
	return obj;
}
