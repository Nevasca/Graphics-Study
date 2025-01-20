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

        UINT m_RtvDescriptorSize{0};
        UINT m_DsvDescriptorSize{0};
        UINT m_CbvSrvDescriptorSize{0};
    
        void CreateDevice();
        void CreateFence();
        void CacheDescriptorSizes();

#if defined(DEBUG) || defined(_DEBUG)
        void EnableDebugLayer();
#endif
    };
}
