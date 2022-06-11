#include <dxgi1_4.h>
#include <d3d12.h>
#include <stdlib.h>

extern "C" {
    #include "renderer.h"
    
    struct Renderer {
        IDXGIFactory3* factory;
        IDXGIAdapter* adapter;
        ID3D12Device* device;
    };

    #define HR_CALL(call) if (FAILED(call)) message_box("D3D12 error")

    Renderer* rd_init() {
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

        return r;
    }

    void rd_free(Renderer* r) {
        r->device->Release();
        r->adapter->Release();
        r->factory->Release();
        free(r);
    }

    void rd_render(Renderer* r) {
        UNUSED(r);
    }
}