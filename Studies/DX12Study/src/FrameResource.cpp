#include "FrameResource.h"

namespace Studies
{
    FrameResource::FrameResource(ID3D12Device& device, UINT passCount, UINT objectCount)
    {
        ThrowIfFailed(device.CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(CommandListAllocator.GetAddressOf())));
        
        PassConstantBuffer = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
        ObjectConstantBuffer = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
    }

    FrameResource::~FrameResource()
    { }
}
