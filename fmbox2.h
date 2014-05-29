#pragma once

#include <stdio.h>

#include <json-c/json.h>
#include <json-c/printbuf.h>
#include <uv.h>
#include <http_parser.h>

extern uv_loop_t *uv_loop;

#include "http.h"
#include "utils.h"


