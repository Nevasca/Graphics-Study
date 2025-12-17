#include "ShapesApplication.h"

#include "Constants.h"
#include "Screen.h"

namespace Studies
{
    void ShapesApplication::Initialize(HWND mainWindow, int windowWidth, int windowHeight)
    {
        assert(mainWindow);

        m_hWindow = mainWindow;
        m_ClientWidth = windowWidth;
        m_ClientHeight = windowHeight;
        
        Screen::OnResize(windowWidth, windowHeight);

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
        
        CreateFrameResources();

        m_Timer.Reset();

        // Execute the initialization commands
        ThrowIfFailed(m_CommandList->Close());
        ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, commandLists);

        FlushCommandQueue();
    }

    void ShapesApplication::Tick()
    {
        m_Timer.Tick();
        
        CalculateFrameStats();
        Draw();
    }

    void ShapesApplication::Draw()
    {
        //TODO
    }

    void ShapesApplication::CreateFrameResources()
    {
        // TODO: implement objects
        const int TOTAL_OBJECTS = 2;

        for(int i = 0; i < Constants::NUM_FRAME_RESOURCES; i++)
        {
            m_FrameResources.emplace_back(std::make_unique<FrameResource>(*m_Device.Get(), 1, TOTAL_OBJECTS));
        }
    }
}
