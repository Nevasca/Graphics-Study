#include "Application.h"

#include <d3dUtil.h>

namespace Studies
{
    void Application::Initialize(HWND mainWindow)
    {
        assert(mainWindow);

        m_hWindow = mainWindow;

#if defined(DEBUG) || defined(_DEBUG)
        EnableDebugLayer();  
#endif

        CreateDevice();
        CreateFence();
        CacheDescriptorSizes();
        Check4xMsaaQuality();
        CreateCommandQueue();
        CreateSwapChain();
        CreateRenderTargetDescriptorHeap();
        CreateDepthStencilDescriptorHeap();
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

    void Application::Check4xMsaaQuality()
    {
        // From DX11 onwards, 4x Msaa support is guaranteed,
        // but we still need to check in what quality it supports

        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
        msQualityLevels.Format = m_BackBufferFormat;
        msQualityLevels.SampleCount = 4;
        msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        msQualityLevels.NumQualityLevels = 0;

        ThrowIfFailed(m_Device->CheckFeatureSupport(
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
            &msQualityLevels,
            sizeof(msQualityLevels)));

        m_4xMsaaQuality = msQualityLevels.NumQualityLevels;

        // Because 4x MSAA is always supported (from DX11 devices onwards), quality level should be grater than 0
        assert(m_4xMsaaQuality > 0 && "Unexpected MSAA quality level");
    }

    void Application::CreateCommandQueue()
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        ThrowIfFailed(m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue)));

        ThrowIfFailed(m_Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(m_CommandListAllocator.GetAddressOf())));

        ThrowIfFailed(m_Device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_CommandListAllocator.Get(),
            nullptr, // Right now we are not issuing commands, just showing how to initialize D3D12, so using null pipeline state for now.
            IID_PPV_ARGS(m_CommandList.GetAddressOf())));

        // Start off in closed state.
        // We are going to call Reset() on it on first time and it needs to be in closed state
        m_CommandList->Close();
    }

    void Application::CreateSwapChain()
    {
        // Release previous swapchain we will be recreating
        m_SwapChain.Reset();

        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferDesc.Width = m_ClientWidth;
        swapChainDesc.BufferDesc.Height = m_ClientHeight;
        swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
        swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
        swapChainDesc.BufferDesc.Format = m_BackBufferFormat;
        swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
        swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

        // Book's approach throws error when creating swap chain
        // swapChainDesc.SampleDesc.Count = m_bIs4xMsaaEnabled ? 4 : 1;
        // swapChainDesc.SampleDesc.Quality = m_bIs4xMsaaEnabled ? m_4xMsaaQuality - 1 : 0;

        // The reason is that you cannot create a MSAA swapchain in modern swapchain
        // we have to create a single-sample swapchain, then create own MSAA render target and explicitly resolve it
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.SampleDesc.Quality = 0;

        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = SWAPCHAIN_BUFFER_COUNT;
        swapChainDesc.OutputWindow = m_hWindow;
        swapChainDesc.Windowed = true;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        ThrowIfFailed(m_DXGIFactory->CreateSwapChain(
            m_CommandQueue.Get(), // Swapchain uses queue to perform flush
            &swapChainDesc,
            m_SwapChain.GetAddressOf()));
    }

    void Application::CreateRenderTargetDescriptorHeap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC renderTargetViewDesc = {};
        renderTargetViewDesc.NumDescriptors = SWAPCHAIN_BUFFER_COUNT;
        renderTargetViewDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        renderTargetViewDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        renderTargetViewDesc.NodeMask = 0;

        ThrowIfFailed(m_Device->CreateDescriptorHeap(
            &renderTargetViewDesc,
            IID_PPV_ARGS(m_RenderTargetViewHeap.GetAddressOf()))); 
    }

    void Application::CreateDepthStencilDescriptorHeap()
    {
        D3D12_DESCRIPTOR_HEAP_DESC depthStencilViewDesc = {};
        depthStencilViewDesc.NumDescriptors = 1;
        depthStencilViewDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        depthStencilViewDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        depthStencilViewDesc.NodeMask = 0;

        ThrowIfFailed(m_Device->CreateDescriptorHeap(
            &depthStencilViewDesc,
            IID_PPV_ARGS(m_DepthStencilViewHeap.GetAddressOf())));
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Application::GetCurrentBackBufferView()
    {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_RenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart(),
            m_CurrentBackBufferIndex,
            m_RtvDescriptorSize);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Application::GetCurrentDepthStencilView()
    {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_DepthStencilViewHeap->GetCPUDescriptorHandleForHeapStart());
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
