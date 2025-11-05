#include "Application.h"
#include "demos/BoxDemo.h"
#include <d3dUtil.h>

#include "Input.h"
#include "Screen.h"

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
        CreateRenderTargetView();
        CreateDepthStencilView();

        m_Timer.Reset();

        m_CurrentDemo = std::make_unique<Demos::BoxDemo>();
        m_CurrentDemo->Initialize(*m_Device.Get(), *m_CommandList.Get());

        // Execute the initialization commands
        ThrowIfFailed(m_CommandList->Close());
        ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, commandLists);

        FlushCommandQueue();
    }

    void Application::Tick()
    {
        m_Timer.Tick();
        m_CurrentDemo->Tick(m_Timer.GetDeltaTime());

        CalculateFrameStats();
        Draw();
    }

    void Application::Draw()
    {
        // Reuse memory associated with command recording
        // We can only reset when the associated command list has finished execution on GPU
        ThrowIfFailed(m_CommandListAllocator->Reset());
        
        // A command list can be reset after it has been added to the command queue via ExecuteCommandList
        // Reusing a command list reuses memory
        ThrowIfFailed(m_CommandList->Reset(m_CommandListAllocator.Get(), m_CurrentDemo->GetInitialPipelineState()));

        // Indicate a state transition on the resource usage
        CD3DX12_RESOURCE_BARRIER backBufferTransitionToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
            GetCurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        m_CommandList->ResourceBarrier(1, &backBufferTransitionToRenderTarget);

        // Set the viewport and scissors rect.
        // This needs to be reset whenever the command list is reset
        ResizeViewport();
        ResizeScissors();

        // Clear back buffer
        m_CommandList->ClearRenderTargetView(
            GetCurrentBackBufferView(),
            DirectX::Colors::BlanchedAlmond,
            0, nullptr);

        // Clear depth buffer
        m_CommandList->ClearDepthStencilView(
            GetCurrentDepthStencilView(),
            D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
            1.0f, 0, 0, nullptr);

        // Specify the buffers we are going to render to
        D3D12_CPU_DESCRIPTOR_HANDLE currentBackBufferView = GetCurrentBackBufferView();
        D3D12_CPU_DESCRIPTOR_HANDLE currentDepthStencilView = GetCurrentDepthStencilView();
        m_CommandList->OMSetRenderTargets(
            1,
            &currentBackBufferView,
            true,
            &currentDepthStencilView);

        m_CurrentDemo->Draw(*m_Device.Get(), *m_CommandList.Get());

        // Indicate a state transition on the resource usage
        CD3DX12_RESOURCE_BARRIER backBufferTransitionToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
            GetCurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);

        m_CommandList->ResourceBarrier(1, &backBufferTransitionToPresent);

        // Done recording commands
        ThrowIfFailed(m_CommandList->Close());

        // Add the command list to the queue for execution
        ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, commandLists);

        // Swap the back and front buffers
        ThrowIfFailed(m_SwapChain->Present(0, 0));
        m_CurrentBackBufferIndex = (m_CurrentBackBufferIndex + 1) % SWAPCHAIN_BUFFER_COUNT;

        // Wait until frame commands are completed
        // This waiting is inefficient and is done for simplicity
        FlushCommandQueue();
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

        // Book says to start closed:
        // "Start off in closed state.
        // We are going to call Reset() on it on first time and it needs to be in closed state"
        // but if we close now, we won't be able to record the initialization commands and then close to execute them
        // m_CommandList->Close();
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

    void Application::CreateRenderTargetView()
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE renderTargetViewHeapHandle(m_RenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart());

        for (UINT i = 0; i < SWAPCHAIN_BUFFER_COUNT; i++)
        {
            // Get the ith buffer in the swap chain
            ThrowIfFailed(m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_SwapChainBuffers[i])));

            // Create a render target view (descriptor) to it
            // Since we've created a typed back buffer, we can leave the descriptor parameter as nullptr
            // If we had created as typeless, we must pass a descriptor param
            m_Device->CreateRenderTargetView(m_SwapChainBuffers[i].Get(), nullptr, renderTargetViewHeapHandle);

            // Next entry in heap
            renderTargetViewHeapHandle.Offset(1, m_RtvDescriptorSize);
        }

        // m_SwapChain->GetBuffer increases the COM reference count to the back buffer,
        // so we must release them when we are finished with it 
        // but as we are using ComPtr for getting it, it will auto call release when getting out of scope here (as smart pointers do) 
    }

    void Application::CreateDepthStencilView()
    {
        D3D12_RESOURCE_DESC depthStencilViewDesc = {};
        depthStencilViewDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthStencilViewDesc.Alignment = 0;
        depthStencilViewDesc.Width = m_ClientWidth;
        depthStencilViewDesc.Height = m_ClientHeight;
        depthStencilViewDesc.DepthOrArraySize = 1;
        depthStencilViewDesc.MipLevels = 1; // For depth and stencil textures, we only need 1 mipmap level
        depthStencilViewDesc.Format = m_DepthStencilFormat;

        // TODO: investigate how to create swapchain and depth stencil view with MSAA in modern swapchain
        depthStencilViewDesc.SampleDesc.Count = 1;
        depthStencilViewDesc.SampleDesc.Quality = 0;

        depthStencilViewDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        depthStencilViewDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = m_DepthStencilFormat;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;

        // There are other more advanced properties to set on heap properties
        // for now we only care about the type (D3D12_HEAP_TYPE_DEFAULT), so we can use the helper constructor
        CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

        ThrowIfFailed(m_Device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &depthStencilViewDesc,
            D3D12_RESOURCE_STATE_COMMON,
            &clearValue,
            IID_PPV_ARGS(m_DepthStencilBuffer.GetAddressOf())));

        // Create descriptor to mip level 0 of entire resource using the format of the resource
        m_Device->CreateDepthStencilView(
            m_DepthStencilBuffer.Get(),
            nullptr, // Since the resource was created with a typed format, this param here can be nullptr
            GetCurrentDepthStencilView());

        // Transition the resource from its initial state to be used as a depth buffer
        CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
                m_DepthStencilBuffer.Get(),
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_DEPTH_WRITE);
        
        m_CommandList->ResourceBarrier(1, &transition);
    }

    void Application::ResizeViewport()
    {
        D3D12_VIEWPORT viewport = {};
        viewport.TopLeftX = 0;
        viewport.TopLeftY = 0;
        viewport.Width = static_cast<float>(m_ClientWidth);
        viewport.Height = static_cast<float>(m_ClientHeight);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        // Using more than one on 'NumViewports' param if for advanced effects
        m_CommandList->RSSetViewports(1, &viewport);

        // The viewport needs to be reset whenever the command list is reset
        
        // A cool thing to do with viewports is split screen,
        // we could have a viewport on half left for player 1 and right left for player 2
    }

    void Application::ResizeScissors()
    {
        D3D12_RECT scissorRect = {};
        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right = m_ClientWidth;
        scissorRect.bottom = m_ClientHeight;

        // Using more than one on 'NumRects' param if for advanced effects
        m_CommandList->RSSetScissorRects(1, &scissorRect);

        // The scissors needs to be reset whenever the command list is reset
    }

    ID3D12Resource* Application::GetCurrentBackBuffer() const
    {
        return m_SwapChainBuffers[m_CurrentBackBufferIndex].Get();
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Application::GetCurrentBackBufferView() const
    {
        return CD3DX12_CPU_DESCRIPTOR_HANDLE(
            m_RenderTargetViewHeap->GetCPUDescriptorHandleForHeapStart(),
            m_CurrentBackBufferIndex,
            m_RtvDescriptorSize);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE Application::GetCurrentDepthStencilView() const
    {
        return m_DepthStencilViewHeap->GetCPUDescriptorHandleForHeapStart();
    }

    void Application::FlushCommandQueue()
    {
        // Advance the fence value to mark commands up to this fence point
        m_CurrentFence++;

        // Add an instruction to the command queue to set a new fence point.  Because we 
        // are on the GPU timeline, the new fence point won't be set until the GPU finishes
        // processing all the commands prior to this Signal().
        ThrowIfFailed(m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence));

        // Wait until the GPU has completed commands up to this fence point
        if(m_Fence->GetCompletedValue() < m_CurrentFence)
        {
            HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);

            // Fire event when GPU hits current fence
            ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFence, eventHandle));

            // Wait until GPU hits current fence
            WaitForSingleObject(eventHandle, INFINITE);
            CloseHandle(eventHandle);
        }
    }

    void Application::CalculateFrameStats()
    {
        constexpr float TIME_PERIOD = 1.f;
        static int frameCount = 0;
        static float timeElapsed = 0.f;

        frameCount++;

        if (m_Timer.GetTime() - timeElapsed < TIME_PERIOD)
        {
            return;
        }

        // fps = frameCount / time period (ignoring division as we are using the time period = 1)
        float fps = static_cast<float>(frameCount);
        float msPerFrame = 1000.f / fps;

        std::wstring windowText = L"Stats: " + std::to_wstring(static_cast<int>(fps)) + L" FPS";
        windowText += L"    " + std::to_wstring(msPerFrame) + L"ms";

        SetWindowText(m_hWindow, windowText.c_str());

        frameCount = 0;
        timeElapsed += TIME_PERIOD;
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

    void Application::OnMouseDown(WPARAM btnState, int x, int y)
    {
        Input::OnMouseDown(btnState, x, y);
        SetCapture(m_hWindow);
    }

    void Application::OnMouseUp(WPARAM btnState, int x, int y)
    {
        Input::OnMouseUp(btnState, x, y);
        ReleaseCapture();
    }

    void Application::OnMouseMove(WPARAM btnState, int x, int y)
    {
        Input::OnMouseMove(btnState, x, y);
    }

    void Application::OnResize(int width, int height)
    {
        Screen::OnResize(width, height);

        m_ClientWidth = width;
        m_ClientHeight = height;

        if(!m_Device)
        {
            return;
        }

        // Flush before changing any resource
        FlushCommandQueue();

        ThrowIfFailed(m_CommandList->Reset(m_CommandListAllocator.Get(), nullptr));

        // Release the previous resources we will be recreating
        for (int i = 0; i < SWAPCHAIN_BUFFER_COUNT; i++)
        {
            m_SwapChainBuffers[i].Reset();
        }

        m_DepthStencilBuffer.Reset();

        // Resize swap chain
        ThrowIfFailed(m_SwapChain->ResizeBuffers(
            SWAPCHAIN_BUFFER_COUNT,
            m_ClientWidth,
            m_ClientHeight,
            m_BackBufferFormat,
            DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

        m_CurrentBackBufferIndex = 0;

        CreateRenderTargetView();
        CreateDepthStencilView();

        ThrowIfFailed(m_CommandList->Close());
        ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, commandLists);

        FlushCommandQueue();
    }
}
