
#include "fmbox2.h"

void xml_free(xml_t *x) {
	if (x->list) {
		for (int i = 0; i < x->length; i++)
			xml_free(x->list[i]);
		arr_free(&x->list);
	}
	if (x->ctxt) {
		if (x->ctxt->myDoc)
			xmlFreeDoc(x->ctxt->myDoc);
		xmlFreeParserCtxt(x->ctxt);
	}
	xmlCleanupParser();	
	free(x);
}

xml_t *xml_new_pusher() {
	xml_t *x = (xml_t *)zalloc(sizeof(xml_t));
	x->ctxt = xmlCreatePushParserCtxt(NULL, NULL, NULL, 0, NULL);
	xmlCtxtUseOptions(x->ctxt, XML_PARSE_RECOVER|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
	return x;
}

void xml_push(xml_t *x, char *buf, int len) {
	if (x->ctxt)
		xmlParseChunk(x->ctxt, buf, len, 0);
}

void xml_push_str(xml_t *x, char *s) {
	xml_push(x, s, strlen(s));
}

static char *xmlNodeGetAttr(xmlNode *n, const char *name) {
	xmlChar *s = xmlGetProp(n, (const xmlChar *)name);
	char *r = strdup((const char *)s);
	xmlFree(s);
	return r;
}

typedef struct _sel_t {
	char level0;
	char *tag;
	char *class_;
	char *has_attr[4]; 
	char *attr_startswith[4];
	char *attr[4];
} sel_t;

static int selector_empty(sel_t *sel) {
	return !sel->tag && !sel->class_ && !sel->has_attr[0];
}

static int selector_match(sel_t *sel, xmlNode *n) {

	if (sel->tag && strcmp((char *)n->name, sel->tag))
		return 0;

	if (sel->class_) {
		xmlChar *class_ = xmlGetProp(n, (const xmlChar *)"class");
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

		if (sel->attr[i])  {
			char *value = xmlNodeGetAttr(n, sel->has_attr[i]);
			if (strcmp(value, sel->attr[i])) {
				free(value);
				return 0;
			}
			free(value);
		}
	}

	return 1;
}

static void walk_element(xml_t *x, FILE *dump_fp, sel_t *sel, xmlNode *a_node, int depth) {

	if (selector_match(sel, a_node)) {
		sel++;
	} else {
		if (sel->level0)
			return ;
	}
	if (selector_empty(sel)) {
		xml_t *x2 = (xml_t *)zalloc(sizeof(xml_t));
		arr_expand(&x->list, x->length+1);
		x2->node = a_node;
		x->list[x->length++] = x2;
		return ;
	}

	xmlNode *cur_node = NULL;
	for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE) {
			if (dump_fp) {
				for (int i = 0; i < depth; i++)
					fprintf(dump_fp, " ");
				fprintf(dump_fp, "%s\r\n", cur_node->name);
			}
			walk_element(x, dump_fp, sel, cur_node->children, depth+1);
		}
	}
}

typedef struct {
	char *s;
	char *id;
	int adv;
	sel_t *sel;
	int attr_i;
} sel_parse_t;

enum {
	S_SPACE,
	S_ID,
	S_DOT = '.',
	S_L_BRACKET = '[',
	S_R_BRACKET = ']',
	S_EQ = '=',
	S_UP_QUOTE = '^',
	S_QUOTE = '\'',
	S_RIGHT = '>', 
	S_EOF,
};

static int s_match(sel_parse_t *p, int type) {
	p->adv = 0;

	switch (type) {

	case S_SPACE:
		for (char *s = p->s; *s; s++) {
			if (*s != ' ')
				break;
			p->adv++;
		}
		break;

	case S_ID:
		for (char *s = p->s; *s; s++) {
			if (!(
					(*s >= '0' && *s <= '9') ||
					(*s >= 'a' && *s <= 'z') ||
					(*s >= 'A' && *s <= 'Z') ||
					*s == '-' || *s == '_'
				))
				break;
			p->adv++;
		}
		break;

	case S_DOT:
	case S_L_BRACKET:
	case S_R_BRACKET:
	case S_EQ:
	case S_UP_QUOTE:
	case S_QUOTE:
	case S_RIGHT:
		if (*p->s == type)
			p->adv = 1;
		break;

	case S_EOF:
		return (*p->s == 0);
	}

	return p->adv;
}

static void s_adv(sel_parse_t *p) {
	p->s += p->adv;
}

static char *s_id(sel_parse_t *p) {
	return strndup(p->s, p->adv);
}

/*
selectors = selector {SPACE selector}+ | EOF
selector = { RIGHT } { ID } { class } { attr }+
class = DOT ID
attr = L_BRAKET ID { attr_eq } R_BRAKET
attr_eq = { UP_QUOTE } EQ QUOTE ID QUOTE
*/

static void s_attr_eq(sel_parse_t *p) {
	int startswith = 0;

	if (s_match(p, S_UP_QUOTE)) {
		startswith = 1;
		s_adv(p);
	}

	if (!s_match(p, S_EQ)) 
		return ;
	s_adv(p);

	if (!s_match(p, S_QUOTE)) 
		return ;
	s_adv(p);

	if (!s_match(p, S_ID)) 
		return ;
	if (startswith) {
		p->sel->attr_startswith[p->attr_i] = s_id(p);
	} else {
		p->sel->attr[p->attr_i] = s_id(p);
	}
	s_adv(p);

	if (!s_match(p, S_QUOTE)) 
		return ;
	s_adv(p);
}

static void s_attr(sel_parse_t *p) {
	if (!s_match(p, S_L_BRACKET))
		return ;
	s_adv(p);

	if (!s_match(p, S_ID))
		return ;
	p->sel->has_attr[p->attr_i] = s_id(p);
	s_adv(p);

	s_attr_eq(p);

	if (!s_match(p, S_R_BRACKET))
		return ;
	s_adv(p);

	p->attr_i++;
}

static void s_class(sel_parse_t *p) {
	if (!s_match(p, S_DOT))
		return ;
	s_adv(p);

	if (!s_match(p, S_ID))
		return ;
	p->sel->class_ = s_id(p);
	s_adv(p);
}

static void s_selector(sel_parse_t *p) {
	p->attr_i = 0;

	if (s_match(p, S_RIGHT)) {
		s_adv(p);
		p->sel->level0 = 1;
	}

	if (s_match(p, S_ID)) {
		p->sel->tag = s_id(p);
		s_adv(p);
	}

	s_class(p);

	while (s_match(p, S_L_BRACKET)) {
		s_attr(p);
	}

	if (!selector_empty(p->sel))
		p->sel++;
}

static void s_selectors(sel_parse_t *p) {
	for (;;) {
		s_selector(p);
		if (!s_match(p, S_SPACE))
			break;
		s_adv(p);
	}
}

static void selectors_build(char *s, sel_t *sel) {
	sel_parse_t p = {};
	p.s = s;
	p.sel = sel;

	s_selectors(&p);
}

static void selector_free(sel_t *sel) {
	if (sel->tag)
		free(sel->tag);
	if (sel->class_)
		free(sel->class_);
	for (int i = 0; i < 4; i++) {
		if (sel->has_attr[i])
			free(sel->has_attr[i]);
		if (sel->attr[i])
			free(sel->attr[i]);
		if (sel->attr_startswith[i])
			free(sel->attr_startswith[i]);
	}
}

static void selectors_free(sel_t *sel) {
	while (!selector_empty(sel)) {
		selector_free(sel);
		sel++;
	}
}

static char *selectors_dump(sel_t *sel) {
	char *s = NULL;

	while (!selector_empty(sel)) {
		if (sel->level0)
			s = str_append(s, "> ");
		if (sel->tag)
			s = str_append(s, sel->tag);
		if (sel->class_) 
			s = str_append_fmt(s, ".%s", sel->class_);
		for (int i = 0; i < 4; i++) {
			if (sel->has_attr[i]) {
				if (sel->attr_startswith[i]) {
					s = str_append_fmt(s, "[%s^='%s']",
						sel->has_attr[i], sel->attr_startswith[i]
						);
				} else if (sel->attr[i]) {
					s = str_append_fmt(s, "[%s='%s']",
						sel->has_attr[i], sel->attr[i]
						);
				} else {
					s = str_append_fmt(s, "[%s]", sel->has_attr[i]);
				}
			}
		}
		s = str_append(s, " ");
		sel++;
	}
	if (s)
		s[strlen(s)-1] = 0;
	return s;
}

xml_t *xml_find(xml_t *x, char *s) {
	sel_t sel[32] = {};
	selectors_build(s, sel);

	xml_t *x2 = (xml_t *)zalloc(sizeof(xml_t));

	for (int i = 0; i < x->length; i++) {
		walk_element(x2, NULL, sel, x->list[i]->node, 0);
	}

	xmlNode *node = x->node;
	if (!node && x->ctxt)
		node = xmlDocGetRootElement(x->ctxt->myDoc);
	if (node)
		walk_element(x2, NULL, sel, node, 0);

	selectors_free(sel);
	return x2;
}

/*
 * === Unit Tests ===
 */

static void test_simple_xml(test_t *t) {
	xml_t *x = xml_new_pusher();

	xml_push_str(x, "<div class='oo'>");
	xml_push_str(x, "<p name='xx'>");
	xml_push_str(x, "<a href='abc'>");
	xml_push_str(x, "<hehe></hehe>");
	xml_push_str(x, "</a>");
	xml_push_str(x, "</p>");
	xml_push_str(x, "</div>");

	xml_t *x2, *x3;

	x2 = xml_find(x, "div.oo p[name='xx'] a[href='abc']");
	test_assert(t, x2->length == 1, "sel 1");
	xml_free(x2);

	x2 = xml_find(x, ".oo [name='xx'] [href='abc']");
	test_assert(t, x2->length == 1, "sel 2");
	xml_free(x2);

	x2 = xml_find(x, "[href]");
	test_assert(t, x2->length == 1, "sel 3");

	x3 = xml_find(x2, "hehe");
	test_assert(t, x3->length == 1, "sub sel 1");
	xml_free(x3);

	if (x2->length == 1) {
		x3 = xml_find(x2->list[0], "hehe");
		test_assert(t, x3->length == 1, "sub sel 2");
		xml_free(x3);
	}

	xml_free(x2);

	test_next(t);
}

static void test_build_selectors(test_t *t) {
	char *s[] = {
		"div.oo p[name='xx'] a",
		"p [a] > [b][c] .oo > [a1][a2='d'][a3^='ha']",
		"div p cc > d > e > f",
		NULL,
	};
	for (int i = 0; s[i]; i++) {
		sel_t sel[32] = {};
		selectors_build(s[i], sel);
		char *d = selectors_dump(sel);

		test_assert(t, !strcmp(d, s[i]), "test build selectors %s", s[i]);

		selectors_free(sel);
		free(d);
	}

	test_next(t);
}

void test_xml(test_t *t) {
	t = test_new(t);
	t->tests[t->nr++] = test_build_selectors;
	t->tests[t->nr++] = test_simple_xml;
	test_next(t);
}

