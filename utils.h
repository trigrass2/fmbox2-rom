#pragma once

static inline int str_startswith(char *s, char *with) {
	return !strncmp(s, with, strlen(with));
}

static inline char *str_append_n(char *s, char *s2, int len2) {
	if (s == NULL)
		return strndup(s2, len2);
	if (s2 == NULL)
		return s;
	int len = strlen(s);
	char *s3 = (char *)malloc(len+len2+1);
	memcpy(s3, s, len);
	memcpy(s3+len, s2, len2);
	s3[len+len2] = 0;
	free(s);
	return s3;
}

static inline char *str_append(char *s, char *s2) {
	return str_append_n(s, s2, strlen(s2));
}

static inline char *str_append_fmt(char *s, const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return str_append(s, buf);
}

static inline int char_hex2dec(char i) {
	if (i >= '0' && i <= '9')
		return i - '0';
	else if (i >= 'a' && i <= 'f')
		return i - 'a' + 10;
	else if (i >= 'A' && i <= 'F')
		return i - 'A' + 10;
	else
		return -1;
}

static void *zalloc(int size) {
	void *r = malloc(size);
	memset(r, 0, size);
	return r;
}

typedef struct _test_t {
	void (*finish)(struct _test_t *);
	void (*tests[32])(struct _test_t *);
	struct _test_t *parent;
	void *data;
	int cur;
	int nr;
	int fail;
} test_t;

static void test_next(test_t *t) {
	if (t->cur < t->nr) {
		t->tests[t->cur++](t);
		return ;
	}
	if (t->finish)
		t->finish(t);
}

static void on_test_finish(test_t *t) {
	test_t *parent = t->parent;
	free(t);
	if (!parent)
		return ;
	test_next(parent);
}

static void test_assert(test_t *t, int ok, const char *fmt, ...) {
	va_list ap;
	char buf[1024];
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	fprintf(stderr, "[%s] %s\n", ok ? "OK" : "FAIL", buf);
}

static test_t *test_new(test_t *parent) {
	test_t *t = (test_t *)zalloc(sizeof(test_t));
	t->parent = parent;
	t->finish = on_test_finish;
	return t;
}

typedef struct {
	int len;
	char data[1];
} arr_t;

static inline void arr_expand(void *in, int count) {
	arr_t *arr;
	if (*(void **)in == NULL) {
		arr = (arr_t *)zalloc(sizeof(arr_t) + sizeof(void *)*32);
		arr->len = 32;
	} else {
		arr = (arr_t *)( (*(char **)in) - sizeof(int) );
		arr = (arr_t *)realloc(arr, arr->len*2);
		arr->len *= 2;
	}
	*(void **)in = (void *)arr->data;
}

static inline void arr_free(void *in) {
	char *p = *(char **)in;
	if (p == NULL)
		return ;
	free(p - sizeof(int));
}

