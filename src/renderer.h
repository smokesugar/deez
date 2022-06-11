#pragma once

#include "common.h"

typedef struct Renderer Renderer;

Renderer* rd_init();
void rd_free(Renderer* r);

void rd_render(Renderer* r);
