#pragma once
#include <string>
#include <d3d12.h>
#include <wrl/client.h>

namespace Studies
{
    class Texture
    {
    public:
        std::string Name; // Unique texture name for lookup
        std::wstring FileName;
        Microsoft::WRL::ComPtr<ID3D12Resource> Resource{nullptr};
        Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeapResource{nullptr};
    };
}
