#pragma once

#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace Studies
{
    class Application
    {
    public:
        void Initialize();

    private:

        Microsoft::WRL::ComPtr<ID3D12Device> m_Device{};
        Microsoft::WRL::ComPtr<IDXGIFactory4> m_DXGIFactory{};
        Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence{};

        Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue{};
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_CommandListAllocator{};
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList{};

        UINT m_RtvDescriptorSize{0};
        UINT m_DsvDescriptorSize{0};
        UINT m_CbvSrvDescriptorSize{0};

        UINT m_4xMsaaQuality{0};
        DXGI_FORMAT m_BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    
        void CreateDevice();
        void CreateFence();
        void CacheDescriptorSizes();
        void Check4xMsaaQuality();
        void CreateCommandQueue();

#if defined(DEBUG) || defined(_DEBUG)
        void EnableDebugLayer();
#endif
    };
}
