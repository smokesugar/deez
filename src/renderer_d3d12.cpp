#include <dxgi1_4.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#include <DirectXMath.h>
using namespace DirectX;

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

extern "C" {
    #include "renderer.h"

    struct CommandList {
        uint64_t fence_val;
        ID3D12CommandAllocator* allocator;
        ID3D12GraphicsCommandList* list;
    };
    
    struct Renderer {
        IDXGIFactory3* factory;
        IDXGIAdapter* adapter;
        ID3D12Device* device;

        ID3D12CommandQueue* queue;
        
        uint64_t fence_val;
        ID3D12Fence* fence;

        IDXGISwapChain3* swapchain;
        ID3D12Resource* swapchain_buffers[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        uint64_t swapchain_fence_vals[DXGI_MAX_SWAP_CHAIN_BUFFERS];

        int cmdl_count;
        CommandList cmdls[DXGI_MAX_SWAP_CHAIN_BUFFERS];

        int free_cmdl_count;
        CommandList* free_cmdls[DXGI_MAX_SWAP_CHAIN_BUFFERS];

        int in_flight_cmdl_count;
        CommandList* in_flight_cmdls[DXGI_MAX_SWAP_CHAIN_BUFFERS];

        ID3D12DescriptorHeap* rtv_heap;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[DXGI_MAX_SWAP_CHAIN_BUFFERS];

        ID3D12DescriptorHeap* binding_heap;
        D3D12_CPU_DESCRIPTOR_HANDLE camera_cbvs_cpu[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        D3D12_GPU_DESCRIPTOR_HANDLE camera_cbvs_gpu[DXGI_MAX_SWAP_CHAIN_BUFFERS];
        void* camera_buffer_ptrs[DXGI_MAX_SWAP_CHAIN_BUFFERS];

        ID3D12Resource* camera_buffer;

        ID3D12RootSignature* root_signature;
        ID3D12PipelineState* pipeline_state;
    };

    #define HR_CALL(call) if (FAILED(call)) message_box("D3D12 error")

    #define ROUND_256(x) ((x + 255) & ~255)

    static void hwnd_size(HWND hwnd, uint32_t* w, uint32_t* h) {
        assert(w != h);

        RECT rect;
        GetClientRect(hwnd, &rect);
        *w = rect.right - rect.left;
        *h = rect.bottom - rect.top;
    }

    static void get_swapchain_buffers(Renderer* r) {
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
        r->swapchain->GetDesc1(&swapchain_desc);

        for (uint32_t i = 0; i < swapchain_desc.BufferCount; ++i) {
            r->swapchain->GetBuffer(i, IID_PPV_ARGS(r->swapchain_buffers + i));
        }
    }

    static void create_rtvs(Renderer* r) {
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
        r->swapchain->GetDesc1(&swapchain_desc);

        for (uint32_t i = 0; i < swapchain_desc.BufferCount; ++i) {
            D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
            rtv_desc.Format = swapchain_desc.Format;
            rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            r->device->CreateRenderTargetView(r->swapchain_buffers[i], &rtv_desc, r->rtvs[i]);
        }
    }

    static ID3DBlob* compile_shader(const wchar_t* path, const char* entry, const char* target) {
        ID3DBlob* code = NULL;
        ID3DBlob* error = NULL;

        if (FAILED(D3DCompileFromFile(path, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry, target, 0, 0, &code, &error))) {
            if (error) {
                debug_message("Shader compilation error:\n");
                debug_message((char*)error->GetBufferPointer());
                debug_message("\n");
                error->Release();
                message_box("Shader compilation error");
            }
            else {
                message_box("Couldn't find shader");
            }

            assert(false);
        }

        assert(code);
        return code;
    }

    static ID3D12RootSignature* create_root_signature(Renderer* r, D3D12_ROOT_SIGNATURE_DESC* desc) {
        ID3DBlob* blob = NULL;
        ID3DBlob* error = NULL;

        if (FAILED(D3D12SerializeRootSignature(desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error))) {
            if (error) {
                debug_message("Root signature error:\n");
                debug_message((char*)error->GetBufferPointer());
                debug_message("\n");
                error->Release();
            }

            message_box("Root signature creation failed");
            assert(false);
        }

        ID3D12RootSignature* rs = NULL;
        r->device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rs));
        blob->Release();

        return rs;
    }

    Renderer* rd_init(void* window) {
        Renderer* r = (Renderer*)calloc(1, sizeof(Renderer));

        #if _DEBUG
        {
            ID3D12Debug* debug = NULL;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
                debug->EnableDebugLayer();
                debug->Release();
                debug_message("D3D12 debug layer enabled\n");
            }
        }
        #endif

        HR_CALL(CreateDXGIFactory(IID_PPV_ARGS(&r->factory)));
        HR_CALL(r->factory->EnumAdapters(1, &r->adapter));
        HR_CALL(D3D12CreateDevice(r->adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&r->device)));

        #if _DEBUG
        {
            ID3D12InfoQueue* iq = NULL;
            if (SUCCEEDED(r->device->QueryInterface(&iq))) {
                iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, 1);
                iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, 1);
                iq->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, 1);

                D3D12_MESSAGE_SEVERITY severity_filter[] = {
                    D3D12_MESSAGE_SEVERITY_INFO
                };

                D3D12_MESSAGE_ID message_filter[] = {
                    D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                    D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                    D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                    D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
                    D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_DEPTHSTENCILVIEW_NOT_SET
                };

                D3D12_INFO_QUEUE_FILTER filter = {};
                filter.DenyList.NumSeverities = ARR_LEN(severity_filter);
                filter.DenyList.pSeverityList = severity_filter;
                filter.DenyList.NumIDs = ARR_LEN(message_filter);
                filter.DenyList.pIDList = message_filter;

                iq->PushStorageFilter(&filter);
                iq->Release();
            }
        }
        #endif

        D3D12_COMMAND_QUEUE_DESC queue_desc = {};
        r->device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&r->queue));

        r->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r->fence));
        
        HWND hwnd = (HWND)window;

        DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {};
        hwnd_size(hwnd, &swapchain_desc.Width, &swapchain_desc.Height);
        swapchain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapchain_desc.SampleDesc.Count = 1;
        swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchain_desc.BufferCount = 2;
        swapchain_desc.Scaling = DXGI_SCALING_NONE;
        swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        
        IDXGISwapChain1* swapchain_1 = NULL;
        r->factory->CreateSwapChainForHwnd(r->queue, hwnd, &swapchain_desc, NULL, NULL, &swapchain_1);
        swapchain_1->QueryInterface(&r->swapchain);
        swapchain_1->Release();

        get_swapchain_buffers(r);

        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.NumDescriptors = DXGI_MAX_SWAP_CHAIN_BUFFERS;

        r->device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&r->rtv_heap));

        for (uint32_t i = 0; i < rtv_heap_desc.NumDescriptors; ++i) {
            uint64_t stride = r->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            D3D12_CPU_DESCRIPTOR_HANDLE heap_start = r->rtv_heap->GetCPUDescriptorHandleForHeapStart();
            r->rtvs[i] = { heap_start.ptr + i * stride  };
        }

        create_rtvs(r);

        D3D12_RESOURCE_DESC camera_buffer_desc = {};
        camera_buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        camera_buffer_desc.Width = ROUND_256(sizeof(XMMATRIX)) * DXGI_MAX_SWAP_CHAIN_BUFFERS;
        camera_buffer_desc.Height = 1;
        camera_buffer_desc.DepthOrArraySize = 1;
        camera_buffer_desc.MipLevels = 1;
        camera_buffer_desc.SampleDesc.Count = 1;
        camera_buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES camera_buffer_heap_props = {};
        camera_buffer_heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;

        r->device->CreateCommittedResource(&camera_buffer_heap_props, D3D12_HEAP_FLAG_NONE, &camera_buffer_desc, D3D12_RESOURCE_STATE_GENERIC_READ, NULL, IID_PPV_ARGS(&r->camera_buffer));

        D3D12_DESCRIPTOR_HEAP_DESC binding_heap_desc = {};
        binding_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        binding_heap_desc.NumDescriptors = DXGI_MAX_SWAP_CHAIN_BUFFERS;
        binding_heap_desc.Flags |= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        r->device->CreateDescriptorHeap(&binding_heap_desc, IID_PPV_ARGS(&r->binding_heap));

        void* camera_buffer_ptr;
        r->camera_buffer->Map(0, NULL, &camera_buffer_ptr);

        for (uint32_t i = 0; i < binding_heap_desc.NumDescriptors; ++i) {
            uint64_t view_stride = r->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = r->binding_heap->GetCPUDescriptorHandleForHeapStart();
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = r->binding_heap->GetGPUDescriptorHandleForHeapStart();
            r->camera_cbvs_cpu[i] = { cpu_start.ptr + i * view_stride };
            r->camera_cbvs_gpu[i] = { gpu_start.ptr + i * view_stride };

            uint64_t buffer_stride = ROUND_256(sizeof(XMMATRIX));
            r->camera_buffer_ptrs[i] = (uint8_t*)camera_buffer_ptr + i * buffer_stride;

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
            cbv_desc.BufferLocation = r->camera_buffer->GetGPUVirtualAddress() + i * buffer_stride;
            cbv_desc.SizeInBytes = (uint32_t)buffer_stride;

            r->device->CreateConstantBufferView(&cbv_desc, r->camera_cbvs_cpu[i]);
        }
        
        ID3DBlob* vs = compile_shader(L"test.hlsl", "vs_main", "vs_5_1");
        ID3DBlob* ps = compile_shader(L"test.hlsl", "ps_main", "ps_5_1");

        D3D12_DESCRIPTOR_RANGE descriptor_ranges[1] = {};
        descriptor_ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        descriptor_ranges[0].NumDescriptors = 1;
        descriptor_ranges[0].BaseShaderRegister = 0;
        descriptor_ranges[0].RegisterSpace = 0;
        descriptor_ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER root_params[1] = {};
        root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_params[0].DescriptorTable.NumDescriptorRanges = ARR_LEN(descriptor_ranges);
        root_params[0].DescriptorTable.pDescriptorRanges = descriptor_ranges;
        root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {};
        root_signature_desc.NumParameters = ARR_LEN(root_params);
        root_signature_desc.pParameters = root_params;

        r->root_signature = create_root_signature(r, &root_signature_desc);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.pRootSignature = r->root_signature;

        pso_desc.VS.BytecodeLength  = vs->GetBufferSize();
        pso_desc.VS.pShaderBytecode = vs->GetBufferPointer();
        pso_desc.PS.BytecodeLength  = ps->GetBufferSize();
        pso_desc.PS.pShaderBytecode = ps->GetBufferPointer();
        
        for (int i = 0; i < ARR_LEN(pso_desc.BlendState.RenderTarget); ++i) {
            D3D12_RENDER_TARGET_BLEND_DESC* blend = pso_desc.BlendState.RenderTarget + i;
            blend->SrcBlend = D3D12_BLEND_ONE;
            blend->DestBlend = D3D12_BLEND_ZERO;
            blend->BlendOp = D3D12_BLEND_OP_ADD;
            blend->SrcBlendAlpha = D3D12_BLEND_ONE;
            blend->DestBlendAlpha = D3D12_BLEND_ZERO;
            blend->BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend->LogicOp = D3D12_LOGIC_OP_NOOP;
            blend->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        pso_desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
                
        pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        pso_desc.RasterizerState.DepthClipEnable = TRUE;
        pso_desc.RasterizerState.FrontCounterClockwise = TRUE;

        pso_desc.DepthStencilState.DepthEnable = TRUE;
        pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pso_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

        pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

        pso_desc.SampleDesc.Count = 1;
        
        r->device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&r->pipeline_state));

        vs->Release();
        ps->Release();

        return r;
    }

    static uint64_t fence_signal(Renderer* r) {
        ++r->fence_val;
        r->queue->Signal(r->fence, r->fence_val);
        return r->fence_val;
    }

    static bool fence_reached(Renderer* r, uint64_t val) {
        return r->fence->GetCompletedValue() >= val;
    }

    static void fence_sync(Renderer* r, uint64_t val) {
        if (r->fence->GetCompletedValue() < val) {
            r->fence->SetEventOnCompletion(val, NULL);
        }
    }

    static void device_flush(Renderer* r) {
        fence_sync(r, fence_signal(r));
    }

    static void update_cmd_lists(Renderer* r) {
        for (int i = r->in_flight_cmdl_count - 1; i >= 0; --i) {
            CommandList* cmdl = r->in_flight_cmdls[i];

            if (fence_reached(r, cmdl->fence_val)) {
                r->in_flight_cmdls[i] = r->in_flight_cmdls[--r->in_flight_cmdl_count];
                r->free_cmdls[r->free_cmdl_count++] = cmdl;
            }
        }
    }

    static void release_swapchain_buffers(Renderer* r) {
        DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
        r->swapchain->GetDesc1(&swapchain_desc);

        for (uint32_t i = 0; i < swapchain_desc.BufferCount; ++i) {
            r->swapchain_buffers[i]->Release();
        }
    }

    void rd_free(Renderer* r) {
        device_flush(r);

        update_cmd_lists(r);
        assert(r->in_flight_cmdl_count == 0);
        assert(r->free_cmdl_count == r->cmdl_count);

        for (int i = 0; i < r->free_cmdl_count; ++i) {
            CommandList* cmdl = r->free_cmdls[i];
            cmdl->list->Release();
            cmdl->allocator->Release();
        }

        r->pipeline_state->Release();
        r->root_signature->Release();

        r->camera_buffer->Release();

        r->binding_heap->Release();
        r->rtv_heap->Release();

        release_swapchain_buffers(r);
        r->swapchain->Release();

        r->fence->Release();
        r->queue->Release();

        r->device->Release();
        r->adapter->Release();
        r->factory->Release();

        free(r);
    }

    void rd_render(Renderer* r) {
        HWND hwnd;
        r->swapchain->GetHwnd(&hwnd);

        uint32_t window_width, window_height;
        hwnd_size(hwnd, &window_width, &window_height);

        if (window_width == 0 || window_height == 0) {
            return;
        }

        {
            DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
            r->swapchain->GetDesc1(&swapchain_desc);

            if (swapchain_desc.Width != window_width || swapchain_desc.Height != window_height) {
                device_flush(r);
                release_swapchain_buffers(r);
                r->swapchain->ResizeBuffers(0, window_width, window_height, DXGI_FORMAT_UNKNOWN, 0);
                get_swapchain_buffers(r);
                create_rtvs(r);
            }
        }

        uint32_t swapchain_index = r->swapchain->GetCurrentBackBufferIndex();
        fence_sync(r, r->swapchain_fence_vals[swapchain_index]);

        update_cmd_lists(r);

        if (r->free_cmdl_count == 0) {
            assert(r->cmdl_count < DXGI_MAX_SWAP_CHAIN_BUFFERS);
            debug_message("Creating a command list\n");

            CommandList* cmdl = r->cmdls + r->cmdl_count++;

            r->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdl->allocator));
            r->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdl->allocator, NULL, IID_PPV_ARGS(&cmdl->list));
            cmdl->list->Close();

            r->free_cmdls[r->free_cmdl_count++] = cmdl;
        }

        CommandList* cmdl = r->free_cmdls[--r->free_cmdl_count];

        cmdl->allocator->Reset();
        cmdl->list->Reset(cmdl->allocator, NULL);

        cmdl->list->SetDescriptorHeaps(1, &r->binding_heap);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = r->swapchain_buffers[swapchain_index];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        cmdl->list->ResourceBarrier(1, &barrier);

        float clear_color[] = { 0.01f, 0.01f, 0.01f, 1.0f };
        cmdl->list->ClearRenderTargetView(r->rtvs[swapchain_index], clear_color, 0, NULL);
        cmdl->list->OMSetRenderTargets(1, r->rtvs + swapchain_index, false, NULL);

        D3D12_VIEWPORT viewport = {};
        viewport.Width = (float)window_width;
        viewport.Height = (float)window_height;
        viewport.MaxDepth = 1.0f;
        cmdl->list->RSSetViewports(1, &viewport);

        D3D12_RECT scissor = {};
        scissor.right = window_width;
        scissor.bottom = window_height;
        cmdl->list->RSSetScissorRects(1, &scissor);

        cmdl->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdl->list->SetGraphicsRootSignature(r->root_signature);
        cmdl->list->SetPipelineState(r->pipeline_state);

        XMMATRIX camera_transform = XMMatrixTranslation(sinf(engine_time() * PI_32), 0.0f, 5.0f);
        XMMATRIX camera_matrix = XMMatrixInverse(NULL, camera_transform) * XMMatrixPerspectiveFovRH(3.14159f * 0.25f, (float)window_width / (float)window_height, 0.1f, 1000.0f);
        memcpy(r->camera_buffer_ptrs[swapchain_index], &camera_matrix, sizeof(camera_matrix));
        cmdl->list->SetGraphicsRootDescriptorTable(0, r->camera_cbvs_gpu[swapchain_index]);

        cmdl->list->DrawInstanced(3, 1, 0, 0);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        cmdl->list->ResourceBarrier(1, &barrier);

        cmdl->list->Close();

        ID3D12CommandList* submissions[] = { cmdl->list };
        r->queue->ExecuteCommandLists(ARR_LEN(submissions), submissions);

        cmdl->fence_val = fence_signal(r);
        r->in_flight_cmdls[r->in_flight_cmdl_count++] = cmdl;
        cmdl = NULL;
       
        r->swapchain->Present(0, 0);
        r->swapchain_fence_vals[swapchain_index] = fence_signal(r);
    }

}
