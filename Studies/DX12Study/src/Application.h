#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace Studies
{
    class Application
    {
    public:
        void Initialize(HWND mainWindow);

    private:

        static constexpr int SWAPCHAIN_BUFFER_COUNT = 2;

        HWND m_hWindow{nullptr};
        
        Microsoft::WRL::ComPtr<ID3D12Device> m_Device{};
        Microsoft::WRL::ComPtr<IDXGIFactory4> m_DXGIFactory{};
        Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence{};

        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue{};
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandListAllocator{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList{};
        Microsoft::WRL::ComPtr<IDXGISwapChain> m_SwapChain{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_RenderTargetViewHeap{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DepthStencilViewHeap{};

        UINT m_RtvDescriptorSize{0};
        UINT m_DsvDescriptorSize{0};
        UINT m_CbvSrvDescriptorSize{0};

        UINT m_4xMsaaQuality{0};
        bool m_bIs4xMsaaEnabled{false};
        DXGI_FORMAT m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        int m_CurrentBackBufferIndex{0};

        int m_ClientWidth{800};
        int m_ClientHeight{600};
    
        void CreateDevice();
        void CreateFence();
        void CacheDescriptorSizes();
        void Check4xMsaaQuality();
        void CreateCommandQueue();
        void CreateSwapChain();
        void CreateRenderTargetDescriptorHeap();
        void CreateDepthStencilDescriptorHeap();

        D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView();
        D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentDepthStencilView();

#if defined(DEBUG) || defined(_DEBUG)
        void EnableDebugLayer();
#endif
    };
}
