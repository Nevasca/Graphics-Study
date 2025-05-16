#pragma once
#include "src/Demo.h"

namespace Studies
{
    namespace Demos
    {
        class BoxDemo : public Demo
        {
        public:

            void Initialize(ID3D12Device& device) override;
            void Tick(float deltaTime) override;
            void Draw(ID3D12Device& device, ID3D12GraphicsCommandList& commandList) override;
        };
    }
}
