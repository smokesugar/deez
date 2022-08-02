#pragma once

#include <DirectXMath.h>
using namespace DirectX;

#include "common.h"

struct Renderer;

struct RDMeshVertex {
    XMFLOAT3 pos;
    XMFLOAT3 norm;
    XMFLOAT2 uv;
};

Renderer* rd_init(void* window);
void rd_free(Renderer* r);

void rd_add_mesh(Renderer* r, RDMeshVertex* vertex_data, uint32_t vertex_count, uint32_t* index_data, uint32_t index_count);

void rd_render(Renderer* r);
