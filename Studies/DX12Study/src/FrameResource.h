#pragma once
#include <d3d12.h>
#include <memory>
#include <wrl/client.h>

#include "ObjectConstants.h"
#include "PassConstants.h"
#include "UploadBuffer.h"

namespace Studies
{
    // Stores the resources needed for the CPU to build command lists per frame
    // Instead of flushing the command queue after every frame, making both CPU and GPU to idle on end and start of a frame respectively
    // we can make use of a circular array of resources called frame resources, usually with 3 frame resource elements
    // The CPU will do any resource updates, build and submit command lists for frame n while GPU works on the previous frames
    class FrameResource
    {
    public:
        // We cannot reset the allocator until GPU is done processing the commands, so each frame needs their own allocator
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandListAllocator;
        
        // We cannot update a cbuffer until GPU is done processing commands that references it, so each frame needs their own cbuffers
        std::unique_ptr<UploadBuffer<PassConstants>> PassConstantBuffer{nullptr};
        
        std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectConstantBuffer{nullptr};
        
        std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialConstantBuffer{nullptr};
        
        // Fence value to mark commands up to this point. This let us check if these frame resources are still in use by the GPU
        UINT64 Fence{0};

        FrameResource(ID3D12Device& device, UINT passCount, UINT objectCount, UINT materialCount = 0);
        ~FrameResource();

        FrameResource(const FrameResource& rhs) = delete;
        FrameResource& operator=(const FrameResource& rhs) = delete;
    };
}
