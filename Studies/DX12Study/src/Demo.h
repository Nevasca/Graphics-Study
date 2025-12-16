#pragma once
#include <d3d12.h>

namespace Studies
{
    class GameTime;

    class Demo
    {
    public:
        virtual void Initialize(ID3D12Device& device, ID3D12GraphicsCommandList& commandList, UINT cbvSrvDescriptorSize) = 0;
        virtual void Tick(const GameTime& gameTime) = 0;
        virtual void Draw(ID3D12Device& device, ID3D12GraphicsCommandList& commandList) = 0;

        virtual ID3D12PipelineState* GetInitialPipelineState() const = 0;

        virtual ~Demo() = default;
        
    protected:
        UINT m_CbvSrvDescriptorSize{0};
    };
}
