#include <Windows.h>
#include <memory.h>
#include <stdio.h>

#include "common.h"
#include "renderer.h"
#include "json.h"

static int64_t counter_start;
static int64_t counter_freq;

struct Events {
    bool closed;
};

void message_box(char* msg) {
    MessageBoxA(NULL, msg, "Deez", 0);
}

void debug_message(char* msg) {
    OutputDebugStringA(msg);
}

char* load_file(char* path, size_t* o_size) {
    HANDLE handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    assert(handle != INVALID_HANDLE_VALUE && "File missing");

    LARGE_INTEGER li;
    GetFileSizeEx(handle, &li);

    size_t s = li.QuadPart;

    char* buf = (char*)malloc(s + 1);
    DWORD read = 0;
    ReadFile(handle, buf, (DWORD)s, &read, NULL);
    assert(read == s);
    buf[read] = '\0';

    CloseHandle(handle);

    if (o_size) {
        *o_size = s;
    }

    return buf;
}

static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM w_param, LPARAM l_param) {
    LRESULT result = 0;

    Events* e = (Events*)GetWindowLongPtrA(window, GWLP_USERDATA);

    switch (msg) {
        case WM_CLOSE:
            e->closed = true;
            break;
            
        default:
            result = DefWindowProcA(window, msg, w_param, l_param);
            break;
    }

    return  result;
}

float engine_time() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    int64_t time_i = li.QuadPart - counter_start;
    double time = (double)time_i / (double)counter_freq;
    return (float)time;
}

static size_t b64_decoded_size(const char *in)
{
	size_t len;
	size_t ret;
	size_t i;

	if (in == NULL)
		return 0;

	len = strlen(in);
	ret = len / 4 * 3;

	for (i=len; i-->0; ) {
		if (in[i] == '=') {
			ret--;
		} else {
			break;
		}
	}

	return ret;
}

static void b64_decode(char* in, void* out, size_t out_max, size_t* out_len) {
    int b64invs[] = { 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58,
	59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5,
	6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28,
	29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
	43, 44, 45, 46, 47, 48, 49, 50, 51 };
    
	size_t len = strlen(in);

    assert(out_len);
    assert(len % 4 == 0);

    *out_len = b64_decoded_size(in);
    assert(*out_len <= out_max);

	for (size_t i=0, j=0; i<len; i+=4, j+=3) {
		int v = b64invs[in[i]-43];
		v = (v << 6) | b64invs[in[i+1]-43];
		v = in[i+2]=='=' ? v << 6 : (v << 6) | b64invs[in[i+2]-43];
		v = in[i+3]=='=' ? v << 6 : (v << 6) | b64invs[in[i+3]-43];

		((char*)out)[j] = (v >> 16) & 0xFF;

        if (in[i + 2] != '=') {
			((char*)out)[j+1] = (v >> 8) & 0xFF;
        }

        if (in[i + 3] != '=') {
            ((char*)out)[j + 2] = v & 0xFF;
        }
	}
}

struct GltfBuffer {
    size_t len;
    void* data;
};

struct GltfBufferView {
    void* ptr;
    size_t len;
};

enum GltfType {
    GLTF_BYTE = 0x1400,
    GLTF_UNSIGNED_BYTE = 0x1401,
    GLTF_SHORT = 0x1402,
    GLTF_UNSIGNED_SHORT = 0x1403,
    GLTF_INT = 0x1404,
    GLTF_UNSIGNED_INT = 0x1405,
    GLTF_FLOAT = 0x1406,
};

struct GltfAccessor {
    void* ptr;
    GltfType type;
    uint32_t count;
    int component_count;
};

static void load_gltf(Renderer* renderer, char* path) {
    char* gltf_str = load_file(path, NULL);
    Json* root = json_parse(gltf_str);
    free(gltf_str);

    char* version = json_string(json_lookup(json_lookup(root, "asset"), "version"));
    
    if (strcmp(version, "2.0") != 0) {
        message_box("Only gltf 2.0 supported");
    }

    Json* buf_list = json_lookup(root, "buffers");
    GltfBuffer* bufs = (GltfBuffer*)calloc(json_array_len(buf_list), sizeof(GltfBuffer));
    int buf_count = 0;

    JSON_ARRAY_FOR(buf_list, buf_info) {
        GltfBuffer* buf = bufs + buf_count++;

        buf->len = (size_t)json_number(json_lookup(buf_info, "byteLength"));
        buf->data = malloc(buf->len);

        const char* base_64_header = "data:application/octet-stream;base64,";

        char* uri = json_string(json_lookup(buf_info, "uri"));
        if (strncmp(uri, base_64_header, strlen(base_64_header)) == 0) {
            char* encoded_data = uri + strlen(base_64_header);
            size_t decoded_len = 0;
            b64_decode(encoded_data, buf->data, buf->len, &decoded_len);
            assert(decoded_len == buf->len);
        }
        else {
            assert("only embedded buffers are supported");
        }
    }

    Json* view_list = json_lookup(root, "bufferViews");
    GltfBufferView* views = (GltfBufferView*)calloc(json_array_len(view_list), sizeof(GltfBufferView));
    int view_count = 0;

    JSON_ARRAY_FOR(view_list, view_info) {
        GltfBufferView* view = views + view_count++;

        view->len = (size_t)json_number(json_lookup(view_info, "byteLength"));

        int buf_index = (int)json_number(json_lookup(view_info, "buffer"));
        assert(buf_index < buf_count);
        GltfBuffer* buf = bufs + buf_index;
        
        size_t offset = (size_t)json_number(json_lookup(view_info, "byteOffset"));
        assert(offset < buf->len);
        view->ptr = (char*)buf->data + offset;
    }

    Json* accessor_list = json_lookup(root, "accessors");
    GltfAccessor* accessors = (GltfAccessor*)calloc(json_array_len(accessor_list), sizeof(GltfAccessor));
    int accessor_count = 0;

    JSON_ARRAY_FOR(accessor_list, accessor_info) {
        GltfAccessor* accessor = accessors + accessor_count++;

        accessor->type = (GltfType)json_number(json_lookup(accessor_info, "componentType"));
        accessor->count = (size_t)json_number(json_lookup(accessor_info, "count"));

        char* type = json_string(json_lookup(accessor_info, "type"));
        if (strcmp(type, "SCALAR") == 0) {
            accessor->component_count = 1;
        }
        else if (strcmp(type, "VEC2") == 0) {
            accessor->component_count = 2;
        }
        else if (strcmp(type, "VEC3") == 0) {
            accessor->component_count = 3;
        }
        else if (strcmp(type, "VEC4") == 0) {
            accessor->component_count = 4;
        }
        else {
            assert(false);
        }

        size_t offset = 0;

        if (json_has(accessor_info, "byteOffset")) {
            offset = (size_t)json_number(json_lookup(accessor_info, "byteOffset"));
        }
        
        int view_index = (int)json_number(json_lookup(accessor_info, "bufferView"));
        assert(view_index < view_count);
        GltfBufferView* view = views + view_index;

        assert(offset < view->len);
        accessor->ptr = (char*)view->ptr + offset;
    }

    JSON_ARRAY_FOR(json_lookup(root, "meshes"), mesh) {
        JSON_ARRAY_FOR(json_lookup(mesh, "primitives"), prim) {
            Json* attributes = json_lookup(prim, "attributes");

            // TODO: check these indices
            GltfAccessor* pos     = accessors + (size_t)json_number(json_lookup(attributes, "POSITION"));
            GltfAccessor* norm    = accessors + (size_t)json_number(json_lookup(attributes, "NORMAL"));
            GltfAccessor* uvs     = accessors + (size_t)json_number(json_lookup(attributes, "TEXCOORD_0"));
            GltfAccessor* indices = accessors + (size_t)json_number(json_lookup(prim, "indices"));

            assert(pos->count == norm->count && pos->count == uvs->count);
            assert(pos->type == GLTF_FLOAT && norm->type == GLTF_FLOAT && uvs->type == GLTF_FLOAT);
            
            uint32_t vertex_count = pos->count;
            RDMeshVertex* vertex_data = (RDMeshVertex*)calloc(vertex_count, sizeof(RDMeshVertex));

            for (uint32_t i = 0; i < vertex_count; ++i) {
                float* pos_ptr = (float*)pos->ptr + i * pos->component_count;
                float* norm_ptr = (float*)norm->ptr + i * norm->component_count;
                float* uvs_ptr = (float*)uvs->ptr + i * uvs->component_count;

                RDMeshVertex* v = vertex_data + i;

                v->pos.x = pos_ptr[0];
                v->pos.y = pos_ptr[1];
                v->pos.z = pos_ptr[2];

                v->norm.x = norm_ptr[0];
                v->norm.y = norm_ptr[1];
                v->norm.z = norm_ptr[2];

                v->uv.x = uvs_ptr[0];
                v->uv.y = uvs_ptr[1];
            }

            uint32_t index_count = indices->count;
            uint32_t* index_data = (uint32_t*)calloc(index_count, sizeof(uint32_t));

            switch (indices->type) {
                case GLTF_UNSIGNED_INT:
                    memcpy(index_data, indices->ptr, index_count * sizeof(uint32_t));
                    break;
                case GLTF_UNSIGNED_SHORT:
                    for (uint32_t i = 0; i < index_count; ++i) {
                        index_data[i] = (uint32_t)((uint16_t*)indices->ptr)[i];
                    }
                    break;
            }

            rd_add_mesh(renderer, vertex_data, vertex_count, index_data, index_count);

            free(index_data);
            free(vertex_data);
        }
    }

    for (int i = 0; i < buf_count; ++i) {
        free(bufs[i].data);
    }

    free(accessors);
    free(views);
    free(bufs);

    json_free(root);
}

int CALLBACK WinMain(HINSTANCE h_instance, HINSTANCE h_prev_instance, LPSTR cmd_line, int cmd_show) {
    UNUSED(h_prev_instance);
    UNUSED(cmd_line);
    UNUSED(cmd_show);

    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    counter_start = li.QuadPart;
    QueryPerformanceFrequency(&li);
    counter_freq = li.QuadPart;

    WNDCLASSA wnd_class = { 0 };
    wnd_class.hInstance = h_instance;
    wnd_class.lpfnWndProc = window_proc;
    wnd_class.lpszClassName = "deez_window_class";

    RegisterClassA(&wnd_class);

    HWND window = CreateWindowExA(0, wnd_class.lpszClassName, "Deez", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, h_instance, NULL);
    ShowWindow(window, SW_MAXIMIZE);

    Events events;
    SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)&events);

    Renderer* r = rd_init(window);

    RDMeshVertex vbuffer_data[] = {
        { { 0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f} },
        { {-0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f} },
        { { 0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f} },
    };

    uint32_t ibuffer_data[] = {
        0, 1, 2
    };

    rd_add_mesh(r, vbuffer_data, ARR_LEN(vbuffer_data), ibuffer_data, ARR_LEN(ibuffer_data));

    load_gltf(r, "monkey.gltf");

    while (true) {
        memset(&events, 0, sizeof(events));
        MSG msg;
        while (PeekMessageA(&msg, window, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (events.closed) {
            break;
        }

        rd_render(r);
    }

    rd_free(r);

    return 0;
}