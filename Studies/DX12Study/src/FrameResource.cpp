#include "FrameResource.h"

namespace Studies
{
    FrameResource::FrameResource(ID3D12Device& device, UINT passCount, UINT objectCount, UINT materialCount)
    {
        ThrowIfFailed(device.CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(CommandListAllocator.GetAddressOf())));
        
        PassConstantBuffer = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
        ObjectConstantBuffer = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
        
        if(materialCount > 0)
        {
            MaterialConstantBuffer = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);
        }
    }

    FrameResource::~FrameResource()
    { }
}
