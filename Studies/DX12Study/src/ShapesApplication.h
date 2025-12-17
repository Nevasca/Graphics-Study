#pragma once
#include <vector>

#include "Application.h"
#include "FrameResource.h"
#include "RenderItem.h"

namespace Studies
{
    class ShapesApplication : public Application
    {
    public:
        void Initialize(HWND mainWindow, int windowWidth, int windowHeight) override;
        void Tick() override;
        void Draw() override;
        
    protected:
        std::vector<std::unique_ptr<FrameResource>> m_FrameResources;
        FrameResource* m_CurrentFrameResource{nullptr};
        int m_CurrentFrameResourceIndex{0};
        
        std::vector<std::unique_ptr<RenderItem>> m_AllRenderItems;
        
        // Render items divided by PSO
        std::vector<RenderItem*> m_OpaqueRenderItems;
        std::vector<RenderItem*> m_TransparentRenderItems;
        
        void CreateFrameResources();
    };
}
