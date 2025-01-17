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
    
        void CreateDevice();

#if defined(DEBUG) || defined(_DEBUG)
        void EnableDebugLayer();
#endif
    };
}
