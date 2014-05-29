#pragma once

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "utils.h"

typedef struct _xml_t {
	xmlParserCtxtPtr ctxt;
	xmlNode *node;
	int _type;
	struct _xml_t **list;
	int length;
} xml_t;

void xml_find(xml_t *x, char *sel, void *data);

xml_t *xml_from_file(char *filename);

xml_t *xml_new_pusher();
void xml_push(xml_t *x, char *buf, int len);
void xml_push_str(xml_t *x, char *buf);

void xml_free(xml_t *x);

void test_xml(test_t *t);

