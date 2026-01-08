#include "ShapesApplication.h"

#include <GeometryGenerator.h>

#include "Constants.h"
#include "Screen.h"
#include "Vertex.h"
#include "VertexBufferUtil.h"

namespace Studies
{
    void ShapesApplication::Initialize(HWND mainWindow, int windowWidth, int windowHeight)
    {
        assert(mainWindow);

        m_hWindow = mainWindow;
        m_ClientWidth = windowWidth;
        m_ClientHeight = windowHeight;
        
        Screen::OnResize(windowWidth, windowHeight);

#if defined(DEBUG) || defined(_DEBUG)
        EnableDebugLayer();  
#endif

        CreateDevice();
        CreateFence();
        CacheDescriptorSizes();
        Check4xMsaaQuality();
        CreateCommandQueue();
        CreateSwapChain();
        CreateRenderTargetDescriptorHeap();
        CreateDepthStencilDescriptorHeap();
        CreateRenderTargetView();
        CreateDepthStencilView();
        
        SetupShapeGeometry();
        SetupRenderItems();

        CreateFrameResources();
        CreateRootSignature();

        m_Timer.Reset();

        // Execute the initialization commands
        ThrowIfFailed(m_CommandList->Close());
        ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
        m_CommandQueue->ExecuteCommandLists(1, commandLists);

        FlushCommandQueue();
    }

    void ShapesApplication::Tick()
    {
        m_Timer.Tick();
        
        CalculateFrameStats();
        
        SetNextFrameResource();
        
        UpdateObjectConstantBuffers();
        UpdatePassConstantBuffer();

        Draw();
    }

    void ShapesApplication::SetNextFrameResource()
    {
        m_CurrentFrameResourceIndex = (m_CurrentFrameResourceIndex + 1) % Constants::NUM_FRAME_RESOURCES;
        m_CurrentFrameResource = m_FrameResources[m_CurrentFrameResourceIndex].get();
        
        // If GPU has not finished processing commands of the current frame, we need to wait
        if(m_CurrentFrameResource->Fence != 0 && m_Fence->GetCompletedValue() < m_CurrentFrameResource->Fence)
        {
            HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
            ThrowIfFailed(m_Fence->SetEventOnCompletion(m_CurrentFrameResource->Fence, eventHandle));
            WaitForSingleObject(eventHandle, INFINITE);
            CloseHandle(eventHandle);
        }
    }

    void ShapesApplication::Draw()
    {
        // TODO: build and submit command list for this frame
        
        m_CurrentFence++;
        m_CurrentFrameResource->Fence = m_CurrentFence;
        
        m_CommandQueue->Signal(m_Fence.Get(), m_CurrentFence);
        
        // GPU could still be working on commands from previous frames, but it's okay as we are not
        // touching any frame resources associated with those frames
    }
    
    void ShapesApplication::UpdateObjectConstantBuffers()
    {
        UploadBuffer<ObjectConstants>* currentObjectConstantBuffer = m_CurrentFrameResource->ObjectConstantBuffer.get();
        
        for(std::unique_ptr<RenderItem>& renderItem : m_AllRenderItems)
        {
            // Only update the constant buffer if the constants have changed
            if(renderItem->NumFramesDirty <= 0)
            {
                continue;
            }
            
            DirectX::XMMATRIX worldMatrix =  DirectX::XMLoadFloat4x4(&renderItem->WorldMatrix);
            ObjectConstants objectConstants;
            DirectX::XMStoreFloat4x4(&objectConstants.World, DirectX::XMMatrixTranspose(worldMatrix));
            
            currentObjectConstantBuffer->CopyData(renderItem->ObjectConstantBufferIndex, objectConstants);
            
            // Next frame resource need to be updated too
            renderItem->NumFramesDirty--;
        }
    }

    void ShapesApplication::UpdatePassConstantBuffer()
    {
        DirectX::XMMATRIX view = DirectX::XMLoadFloat4x4(&m_View);
        DirectX::XMMATRIX proj = DirectX::XMLoadFloat4x4(&m_Proj);
        
        DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);

        DirectX::XMVECTOR viewDeterminant = DirectX::XMMatrixDeterminant(view);
        DirectX::XMMATRIX invView = DirectX::XMMatrixInverse(&viewDeterminant, view);
        
        DirectX::XMVECTOR projDeterminant = DirectX::XMMatrixDeterminant(proj);
        DirectX::XMMATRIX invProj = DirectX::XMMatrixInverse(&projDeterminant, proj);
        
        DirectX::XMVECTOR viewProjDeterminant = DirectX::XMMatrixDeterminant(viewProj);
        DirectX::XMMATRIX invViewProj = DirectX::XMMatrixInverse(&viewProjDeterminant, viewProj);
        
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.View, DirectX::XMMatrixTranspose(view));
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.InvView, DirectX::XMMatrixTranspose(invView));
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.Proj, DirectX::XMMatrixTranspose(proj));
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.InvProj, DirectX::XMMatrixTranspose(invProj));
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.ViewProj, DirectX::XMMatrixTranspose(viewProj));
        DirectX::XMStoreFloat4x4(&m_MainPassConstants.InvViewProj, DirectX::XMMatrixTranspose(invViewProj));

        m_MainPassConstants.EyePositionWorld = m_EyePositionWorld;

        m_MainPassConstants.RenderTargetSize = DirectX::XMFLOAT2{static_cast<float>(m_ClientWidth), static_cast<float>(m_ClientHeight)};
        m_MainPassConstants.InvRenderTargetSize = DirectX::XMFLOAT2{1.f / m_ClientWidth, 1.f / m_ClientHeight};
        
        m_MainPassConstants.NearZ = 1.f;
        m_MainPassConstants.FarZ = 1000.f;
        
        m_MainPassConstants.TotalTime = m_Timer.GetTime();
        m_MainPassConstants.DeltaTime = m_Timer.GetDeltaTime();
        
        UploadBuffer<PassConstants>* currentPassConstantBuffer = m_CurrentFrameResource->PassConstantBuffer.get();
        currentPassConstantBuffer->CopyData(0, m_MainPassConstants);
    }

    void ShapesApplication::CreateRootSignature()
    {
        // We should not go overboard with number of constant buffers in our shaders for performance reasons
        // It's recommended to keep them under five

        CD3DX12_ROOT_PARAMETER rootParameters[2];
        
        CD3DX12_DESCRIPTOR_RANGE constantBufferViewTablePerObject;
        constantBufferViewTablePerObject.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
        rootParameters[0].InitAsDescriptorTable(1, &constantBufferViewTablePerObject);
        
        CD3DX12_DESCRIPTOR_RANGE constantBufferViewTablePass;
        constantBufferViewTablePass.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
        rootParameters[1].InitAsDescriptorTable(1, &constantBufferViewTablePass);
        
        CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc
        {
            2,
            rootParameters,
            0,
            nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        };
        
        Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature{nullptr};
        Microsoft::WRL::ComPtr<ID3DBlob> errorBlob{nullptr};

        HRESULT hr = D3D12SerializeRootSignature(
            &rootSignatureDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            serializedRootSignature.GetAddressOf(),
            errorBlob.GetAddressOf());

        ThrowIfFailed(hr);
        
        ThrowIfFailed(m_Device->CreateRootSignature(
            0,
            serializedRootSignature->GetBufferPointer(),
            serializedRootSignature->GetBufferSize(),
            IID_PPV_ARGS(&m_RootSignature)));
    }

    void ShapesApplication::CreateFrameResources()
    {
        UINT objectCount = static_cast<UINT>(m_AllRenderItems.size());

        for(int i = 0; i < Constants::NUM_FRAME_RESOURCES; i++)
        {
            m_FrameResources.emplace_back(std::make_unique<FrameResource>(*m_Device.Get(), 1, objectCount));
        }
    }

    void ShapesApplication::SetupShapeGeometry()
    {
        GeometryGenerator generator{};
        
        GeometryGenerator::MeshData box = generator.CreateBox(1.5f, 0.5f, 1.5f, 3);
        GeometryGenerator::MeshData grid = generator.CreateGrid(20.f, 30.f, 60, 40);
        GeometryGenerator::MeshData sphere = generator.CreateSphere(0.5f, 20, 20);
        GeometryGenerator::MeshData cylinder = generator.CreateCylinder(0.5f, 0.3f, 3.f, 20, 20);
        
        // We are concatenating all the geometry into one big vertex/index buffer
        // So we define the regions to cover each submesh
        UINT boxVertexOffset = 0;
        UINT gridVertexOffset = static_cast<UINT>(box.Vertices.size());
        UINT sphereVertexOffset = gridVertexOffset + static_cast<UINT>(grid.Vertices.size());
        UINT cylinderVertexOffset = sphereVertexOffset + static_cast<UINT>(sphere.Vertices.size());
        
        UINT boxIndexOffset = 0;
        UINT gridIndexOffset = static_cast<UINT>(box.Indices32.size());
        UINT sphereIndexOffset = gridIndexOffset + static_cast<UINT>(grid.Indices32.size());
        UINT cylinderIndexOffset = sphereIndexOffset + static_cast<UINT>(sphere.Indices32.size());
        
        // Define the SubmeshGeometry to the different regions of the vertex/index buffer
        SubmeshGeometry boxSubmesh{};
        boxSubmesh.IndexCount = static_cast<UINT>(box.Indices32.size());
        boxSubmesh.BaseVertexLocation = static_cast<int>(boxVertexOffset);
        boxSubmesh.StartIndexLocation = boxIndexOffset;
        
        SubmeshGeometry gridSubmesh{};
        gridSubmesh.IndexCount = static_cast<UINT>(grid.Indices32.size());
        gridSubmesh.BaseVertexLocation = static_cast<int>(gridVertexOffset);
        gridSubmesh.StartIndexLocation = gridIndexOffset;
        
        SubmeshGeometry sphereSubmesh{};
        sphereSubmesh.IndexCount = static_cast<UINT>(sphere.Indices32.size());
        sphereSubmesh.BaseVertexLocation = static_cast<int>(sphereVertexOffset);
        sphereSubmesh.StartIndexLocation = sphereIndexOffset;
        
        SubmeshGeometry cylinderSubmesh{};
        cylinderSubmesh.IndexCount = static_cast<UINT>(cylinder.Indices32.size());
        cylinderSubmesh.BaseVertexLocation = static_cast<int>(cylinderVertexOffset);
        cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
        
        // Extract the vertex elements we are interested in and pack them on a single vertex buffer
        auto totalVertexCount = box.Vertices.size();
        totalVertexCount += grid.Vertices.size();
        totalVertexCount += sphere.Vertices.size();
        totalVertexCount += cylinder.Vertices.size();
        
        std::vector<Vertex> vertices{totalVertexCount};
        UINT k = 0;

        for (size_t i = 0; i < box.Vertices.size(); i++, k++)
        {
            vertices[k].Position = box.Vertices[i].Position;
            vertices[k].Color = DirectX::XMFLOAT4{DirectX::Colors::DarkGreen};
        }
        
        for (size_t i = 0; i < grid.Vertices.size(); i++, k++)
        {
            vertices[k].Position = grid.Vertices[i].Position;
            vertices[k].Color = DirectX::XMFLOAT4{DirectX::Colors::ForestGreen};
        }
        
        for (size_t i = 0; i < sphere.Vertices.size(); i++, k++)
        {
            vertices[k].Position = sphere.Vertices[i].Position;
            vertices[k].Color = DirectX::XMFLOAT4{DirectX::Colors::Crimson};
        }
        
        for (size_t i = 0; i < cylinder.Vertices.size(); i++, k++)
        {
            vertices[k].Position = cylinder.Vertices[i].Position;
            vertices[k].Color = DirectX::XMFLOAT4{DirectX::Colors::SteelBlue};
        }
        
        std::vector<std::uint16_t> indices{};
        indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
        indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
        indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
        indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
        
        // Create the MeshGeometry
        const UINT vertexBufferSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
        const UINT indexBufferSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

        std::unique_ptr<MeshGeometry> geometry = std::make_unique<MeshGeometry>();
        geometry->Name = "shapeGeometry";
        
        ThrowIfFailed(D3DCreateBlob(vertexBufferSize, &geometry->VertexBufferCPU));
        CopyMemory(geometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vertexBufferSize);

        ThrowIfFailed(D3DCreateBlob(indexBufferSize, &geometry->IndexBufferCPU));
        CopyMemory(geometry->IndexBufferCPU->GetBufferPointer(), indices.data(), indexBufferSize);
        
        geometry->VertexBufferGPU = VertexBufferUtil::CreateDefaultBuffer(m_Device.Get(), m_CommandList.Get(), vertices.data(), vertexBufferSize, geometry->VertexBufferUploader);
        geometry->IndexBufferGPU = VertexBufferUtil::CreateDefaultBuffer(m_Device.Get(), m_CommandList.Get(), indices.data(), indexBufferSize, geometry->IndexBufferUploader);
        
        geometry->VertexByteStride = sizeof(Vertex);
        geometry->VertexBufferByteSize = vertexBufferSize;
        geometry->IndexFormat = DXGI_FORMAT_R16_UINT;
        geometry->IndexBufferByteSize = indexBufferSize;
        
        geometry->DrawArgs["box"] = boxSubmesh;
        geometry->DrawArgs["grid"] = gridSubmesh;
        geometry->DrawArgs["sphere"] = sphereSubmesh;
        geometry->DrawArgs["cylinder"] = cylinderSubmesh;
        
        m_Geometries[geometry->Name] = std::move(geometry);
    }

    void ShapesApplication::SetupRenderItems()
    {
        using namespace DirectX;

        std::unique_ptr<RenderItem> boxRenderItem = std::make_unique<RenderItem>();
        XMStoreFloat4x4(&boxRenderItem->WorldMatrix, XMMatrixScaling(2.f, 2.f, 2.f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
        boxRenderItem->ObjectConstantBufferIndex = 0;
        boxRenderItem->Geometry = m_Geometries["shapeGeometry"].get();
        boxRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        boxRenderItem->IndexCount = boxRenderItem->Geometry->DrawArgs["box"].IndexCount;
        boxRenderItem->StartIndexLocation = boxRenderItem->Geometry->DrawArgs["box"].StartIndexLocation;
        boxRenderItem->BaseVertexLocation = boxRenderItem->Geometry->DrawArgs["box"].BaseVertexLocation;
        m_AllRenderItems.emplace_back(std::move(boxRenderItem));
        
        std::unique_ptr<RenderItem> gridRenderItem = std::make_unique<RenderItem>();
        gridRenderItem->WorldMatrix = MathHelper::Identity4x4();
        gridRenderItem->ObjectConstantBufferIndex = 1;
        gridRenderItem->Geometry = m_Geometries["shapeGeometry"].get();
        gridRenderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        gridRenderItem->IndexCount = gridRenderItem->Geometry->DrawArgs["grid"].IndexCount;
        gridRenderItem->StartIndexLocation = gridRenderItem->Geometry->DrawArgs["grid"].StartIndexLocation;
        gridRenderItem->BaseVertexLocation = gridRenderItem->Geometry->DrawArgs["grid"].BaseVertexLocation;
        m_AllRenderItems.emplace_back(std::move(gridRenderItem));
        
        // Build columns and spheres in rows
        UINT objectConstantBufferIndex = 2;
        for (int i = 0; i < 5; i++)
        {
            std::unique_ptr<RenderItem> leftCylinderItem = std::make_unique<RenderItem>();
            std::unique_ptr<RenderItem> rightCylinderItem = std::make_unique<RenderItem>();
            std::unique_ptr<RenderItem> leftSphereItem = std::make_unique<RenderItem>();
            std::unique_ptr<RenderItem> rightSphereItem = std::make_unique<RenderItem>();
            
            XMMATRIX leftCylinderWorld = XMMatrixTranslation(-5.f, 1.5f, -10.f + static_cast<float>(i) * 5.f);
            XMMATRIX rightCylinderWorld = XMMatrixTranslation(5.f, 1.5f, -10.f + static_cast<float>(i) * 5.f);
            
            XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.f, 3.5f, -10.f + static_cast<float>(i) * 5.f);
            XMMATRIX rightSphereWorld = XMMatrixTranslation(5.f, 3.5f, -10.f + static_cast<float>(i) * 5.f);
            
            XMStoreFloat4x4(&leftCylinderItem->WorldMatrix, leftCylinderWorld);
            leftCylinderItem->ObjectConstantBufferIndex = objectConstantBufferIndex++;
            leftCylinderItem->Geometry = m_Geometries["shapeGeometry"].get();
            leftCylinderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            leftCylinderItem->IndexCount = leftCylinderItem->Geometry->DrawArgs["cylinder"].IndexCount;
            leftCylinderItem->StartIndexLocation = leftCylinderItem->Geometry->DrawArgs["cylinder"].StartIndexLocation;
            leftCylinderItem->BaseVertexLocation = leftCylinderItem->Geometry->DrawArgs["cylinder"].BaseVertexLocation;
            
            XMStoreFloat4x4(&rightCylinderItem->WorldMatrix, rightCylinderWorld);
            rightCylinderItem->ObjectConstantBufferIndex = objectConstantBufferIndex++;
            rightCylinderItem->Geometry = m_Geometries["shapeGeometry"].get();
            rightCylinderItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            rightCylinderItem->IndexCount = rightCylinderItem->Geometry->DrawArgs["cylinder"].IndexCount;
            rightCylinderItem->StartIndexLocation = rightCylinderItem->Geometry->DrawArgs["cylinder"].StartIndexLocation;
            rightCylinderItem->BaseVertexLocation = rightCylinderItem->Geometry->DrawArgs["cylinder"].BaseVertexLocation;
            
            XMStoreFloat4x4(&leftSphereItem->WorldMatrix, leftSphereWorld);
            leftSphereItem->ObjectConstantBufferIndex = objectConstantBufferIndex++;
            leftSphereItem->Geometry = m_Geometries["shapeGeometry"].get();
            leftSphereItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            leftSphereItem->IndexCount = leftCylinderItem->Geometry->DrawArgs["sphere"].IndexCount;
            leftSphereItem->StartIndexLocation = leftCylinderItem->Geometry->DrawArgs["sphere"].StartIndexLocation;
            leftSphereItem->BaseVertexLocation = leftCylinderItem->Geometry->DrawArgs["sphere"].BaseVertexLocation;
            
            XMStoreFloat4x4(&rightSphereItem->WorldMatrix, rightSphereWorld);
            rightSphereItem->ObjectConstantBufferIndex = objectConstantBufferIndex++;
            rightSphereItem->Geometry = m_Geometries["shapeGeometry"].get();
            rightSphereItem->PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            rightSphereItem->IndexCount = leftCylinderItem->Geometry->DrawArgs["sphere"].IndexCount;
            rightSphereItem->StartIndexLocation = leftCylinderItem->Geometry->DrawArgs["sphere"].StartIndexLocation;
            rightSphereItem->BaseVertexLocation = leftCylinderItem->Geometry->DrawArgs["sphere"].BaseVertexLocation;
            
            m_AllRenderItems.emplace_back(std::move(leftCylinderItem));
            m_AllRenderItems.emplace_back(std::move(rightCylinderItem));
            m_AllRenderItems.emplace_back(std::move(leftSphereItem));
            m_AllRenderItems.emplace_back(std::move(rightSphereItem));
        }
        
        // All render items are opaque in this demo
        for (const auto& renderItem : m_AllRenderItems)
        {
            m_OpaqueRenderItems.push_back(renderItem.get());
        }
    }
}
