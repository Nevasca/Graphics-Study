#pragma once
#include <d3d12.h>

namespace Studies
{
    class Demo
    {
    public:
        virtual void Initialize(ID3D12Device& device, ID3D12GraphicsCommandList& commandList) = 0;
        virtual void Tick(float deltaTime) = 0;
        virtual void Draw(ID3D12Device& device, ID3D12GraphicsCommandList& commandList) = 0;

        virtual ~Demo() = default;
    };
}
