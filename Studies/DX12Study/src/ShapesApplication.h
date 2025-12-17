#pragma once
#include <vector>

#include "Application.h"
#include "FrameResource.h"

namespace Studies
{
    class ShapesApplication : public Application
    {
    public:
        void Initialize(HWND mainWindow, int windowWidth, int windowHeight) override;
        void Tick() override;
        void Draw() override;
        
    protected:
        static constexpr int NUM_FRAME_RESOURCES = 3;
        
        std::vector<std::unique_ptr<FrameResource>> m_FrameResources;
        FrameResource* m_CurrentFrameResource{nullptr};
        int m_CurrentFrameResourceIndex{0};
        
        void CreateFrameResources();
    };
}
