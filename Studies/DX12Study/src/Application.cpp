#include "Application.h"

#include <d3dUtil.h>

namespace Studies
{
    void Application::Initialize()
    {
#if defined(DEBUG) || defined(_DEBUG)
        EnableDebugLayer();  
#endif

        CreateDevice();
        CreateFence();
        CacheDescriptorSizes();
    }

    void Application::CreateDevice()
    {
        ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&m_DXGIFactory)));
        
        HRESULT hardwareResult = D3D12CreateDevice(
            nullptr, // nullptr to use default adapter
            D3D_FEATURE_LEVEL_11_0, // min feature level the application supports
            IID_PPV_ARGS(&m_Device));

        if (SUCCEEDED(hardwareResult))
        {
            return;
        }

        // No hardware device available on current system, fallback to WARP device
        Microsoft::WRL::ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(m_DXGIFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

        ThrowIfFailed(D3D12CreateDevice(
            warpAdapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&m_Device)));
    }

    void Application::CreateFence()
    {
        ThrowIfFailed(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)));
    }

    void Application::CacheDescriptorSizes()
    {
        // Descriptor sizes can vary across GPUs so we need to query this information,
        // cache them so we can use when we need them for various descriptor types

        // Render target resource
        m_RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Depth/Stencil resource
        m_DsvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        // Constant buffer, shader resource and unordered access resource
        m_CbvSrvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

#if defined(DEBUG) || defined(_DEBUG)
    void Application::EnableDebugLayer()
    {
        // Enable the D3D12 debug layer, so we can have extra debugging messages sent to output window,
        // such as "D3D12 ERROR: ID3D12CommandList::Reset: Reset fails because the command list was not closed."
        Microsoft::WRL::ComPtr<ID3D12Debug> debugController;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
        debugController->EnableDebugLayer();
    }
#endif
}
