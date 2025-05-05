#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "GameTime.h"

namespace Studies
{
    class Application
    {
    public:
        void Initialize(HWND mainWindow);
        void Tick();

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

        Microsoft::WRL::ComPtr<ID3D12Resource> m_SwapChainBuffers[SWAPCHAIN_BUFFER_COUNT];
        Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthStencilBuffer{};

        UINT m_RtvDescriptorSize{0};
        UINT m_DsvDescriptorSize{0};
        UINT m_CbvSrvDescriptorSize{0};

        UINT m_4xMsaaQuality{0};
        bool m_bIs4xMsaaEnabled{false};
        DXGI_FORMAT m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT m_DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
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
        void CreateRenderTargetView();
        void CreateDepthStencilView();
        void ResizeViewport();
        void ResizeScissors();

        D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView() const;
        D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentDepthStencilView() const;

#if defined(DEBUG) || defined(_DEBUG)
        void EnableDebugLayer();
#endif

    private:
        GameTime m_Timer{};
    };
}
