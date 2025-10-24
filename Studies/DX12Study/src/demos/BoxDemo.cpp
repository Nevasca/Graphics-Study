#include "BoxDemo.h"

#include <DirectXColors.h>
#include <wrl/client.h>

#include "src/Vertex.h"
#include "src/VertexBufferUtil.h"

#include <d3dUtil.h>

#include "src/ShaderUtil.h"

namespace Studies
{
    namespace Demos
    {
        void BoxDemo::Initialize(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            CreateConstantBufferViewHeap(device);
            CreateConstantBufferView(device);
            CreateRootSignature(device);

            SetupShader();
            SetupCube(device, commandList);

            CreatePipelineStateObject(device);
        }

        void BoxDemo::Tick(float deltaTime)
        { }

        void BoxDemo::Draw(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            // When not using index buffers, we use ID3D12GraphicsCommandList::DrawInstanced
            // When using index buffers, we use ID3D12GraphicsCommandList::DrawIndexedInstanced

            // For performance, we should make the root signature as small as possible
            // and try to minimize the number of times we change the root signature per rendering frame
            commandList.SetGraphicsRootSignature(m_rootSignature.Get());

            ID3D12DescriptorHeap* descriptorHeaps[] = { m_constantBufferViewHeap.Get() };
            commandList.SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

            // If we were drawing more than one object, we would need to offset the constant buffer view like this:
            // CD3DX12_GPU_DESCRIPTOR_HANDLE constantBufferView{m_constantBufferViewHeap->GetGPUDescriptorHandleForHeapStart()};
            // constantBufferView.Offset(cbvIndex, m_CbvSrvDescriptorSize);

            // As for this demo we have only one object, we can simply do this:
            commandList.SetGraphicsRootDescriptorTable(0, m_constantBufferViewHeap->GetGPUDescriptorHandleForHeapStart());
        }

        void BoxDemo::CreateConstantBufferViewHeap(ID3D12Device& device)
        {
            D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
            // Since on this BoxDemo we don't have an SRV or UAV descriptors, and we are going to render only one object, we just need 1 descriptor in the heap
            cbvHeapDesc.NumDescriptors = 1;
            cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            // D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE indicate that the descriptor will be accessed by shader programs
            cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            cbvHeapDesc.NodeMask = 0;

            ThrowIfFailed(device.CreateDescriptorHeap(
                &cbvHeapDesc,
                IID_PPV_ARGS(m_constantBufferViewHeap.GetAddressOf())));
        }

        void BoxDemo::CreateConstantBufferView(ID3D12Device& device)
        {
            m_objectConstantBuffer = std::make_unique<UploadBuffer<ObjectConstants>>(device, 1, true);

            UINT objectConstantBufferSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

            // Address to start of the buffer (0th constant buffer)
            D3D12_GPU_VIRTUAL_ADDRESS constantBufferAddress = m_objectConstantBuffer->GetResource()->GetGPUVirtualAddress();

            // Offset to the ith object constant buffer in the buffer. In our case, we are only using one object
            int boxConstantBufferIndex = 0;
            constantBufferAddress += boxConstantBufferIndex * objectConstantBufferSize;

            // D3D12_CONSTANT_BUFFER_VIEW_DESC describes a subset of the constant buffer resource to bind to the HLSL
            D3D12_CONSTANT_BUFFER_VIEW_DESC constantBufferViewDesc{};
            constantBufferViewDesc.BufferLocation = constantBufferAddress; // Must be multiple of 256 bytes
            constantBufferViewDesc.SizeInBytes = objectConstantBufferSize; // Must be multiple of 256 bytes

            device.CreateConstantBufferView(
                &constantBufferViewDesc,
                m_constantBufferViewHeap->GetCPUDescriptorHandleForHeapStart());
        }

        void BoxDemo::CreateRootSignature(ID3D12Device& device)
        {
            // Root signature defines what resources the application will bind to the rendering pipeline before
            // a draw call can be executed. It does not actually do any resource binding

            // "If we think shader programs as functions, and the input resources the shaders expects as function params,
            // then the root signature can be thought of as defining a function signature"

            // Root signature is defined by an array of root params that describe the resources the shaders expects
            // Root param can be a descriptor table, root descriptor or root constants
            // In the box demo we will use only a descriptor table
            
            CD3DX12_ROOT_PARAMETER slotRootParameter[1];

            // Create a single descriptor table of CBVs
            CD3DX12_DESCRIPTOR_RANGE constantBufferViewTable;
            constantBufferViewTable.Init(
                D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
                1, // Number of descriptors in table
                0); // Base shader register arguments are bound to for this root parameter (register b(0) in this case)

            slotRootParameter[0].InitAsDescriptorTable(1, &constantBufferViewTable);

            // A root signature is an array of root parameters
            CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc{
                1,
                slotRootParameter,
                0,
                nullptr,
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};

            // Create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
            Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSignature = nullptr;
            Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;

            HRESULT hr = D3D12SerializeRootSignature(
                &rootSignatureDesc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                serializedRootSignature.GetAddressOf(),
                errorBlob.GetAddressOf());

            ThrowIfFailed(hr);

            ThrowIfFailed(device.CreateRootSignature(
                0,
                serializedRootSignature->GetBufferPointer(),
                serializedRootSignature->GetBufferSize(),
                IID_PPV_ARGS(&m_rootSignature)));
        }

        void BoxDemo::CreatePipelineStateObject(ID3D12Device& device)
        {
            // Objects that control the state of the graphics pipeline are actually bound for use via
            // an aggregate called pipeline state object
            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineStateDescription{};
            ZeroMemory(&pipelineStateDescription, sizeof(pipelineStateDescription));

            // InputLayout description is simply an array of input element descriptions and the number of elements
            pipelineStateDescription.InputLayout = {
                m_inputElementDescriptions.data(),
                static_cast<UINT>(m_inputElementDescriptions.size()),
            };

            pipelineStateDescription.pRootSignature = m_rootSignature.Get();

            // On the book author reinterpreters GetBufferPointer into BYTE* (reinterpreter_cast<BYTE*>m_vertexShaderByteCode->GetBufferPointer())
            // Decided to not do that as D3D12_SHADER_BYTECODE expects a const void* and GetBufferPointer already returns void*
            pipelineStateDescription.VS = {
                m_vertexShaderByteCode->GetBufferPointer(),
                m_vertexShaderByteCode->GetBufferSize()
            };

            pipelineStateDescription.PS = {
                m_pixelShaderByteCode->GetBufferPointer(),
                m_pixelShaderByteCode->GetBufferSize()
            };

            // On RasterizerState we can set fill mode (solid or wireframe), cull mode (back, front) and some other params
            // We can create a rasterizer description with default values by passing the dummy type D3D12_DEFAULT on the constructor
            pipelineStateDescription.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

            // The dummy type D3D12_DEFAULT can be used on other descriptions as well
            pipelineStateDescription.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            pipelineStateDescription.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

            // Multisampling can take up to 32 samples. This 32-bit integer value is used to enable/disable the samples.
            // For example, if you turn off the 5th bit, then the 5th sample will not be taken.
            // Generally the default 0xffffffff is used
            pipelineStateDescription.SampleMask = UINT_MAX;

            // Specifies the primitive topology type (undefined, point, line, triangle, patch)
            pipelineStateDescription.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

            // Number of render targets we are using simultaneously
            pipelineStateDescription.NumRenderTargets = 1;

            // TODO: hard coding what is used on Application::m_BackBufferFormat, refactor to have this exposed or passed somewhere
            pipelineStateDescription.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

            // TODO: investigate how to create with multisampling, books' approach on swap chain with multisampling
            // does not work anymore. Hard coding a sample desc not using multisampling
            pipelineStateDescription.SampleDesc.Count = 1;
            pipelineStateDescription.SampleDesc.Quality = 0;
            
            // TODO: hard coding what is used on Application::m_DepthStencilFormat, refactor to have this exposed or passed somewhere
            pipelineStateDescription.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

            ThrowIfFailed(device.CreateGraphicsPipelineState(
                &pipelineStateDescription,
                IID_PPV_ARGS(&m_pipelineStateObject)));
        }

        void BoxDemo::SetupShader()
        {
            const std::wstring shaderPath = L"data//color.hlsl";

            m_vertexShaderByteCode = ShaderUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
            m_pixelShaderByteCode = ShaderUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");

            // Input elements linking to what the shader expects. If we feed in vertices that do not supply all the inputs a vertex shader expects, an error will result
            // The vertex data and input signature do not need to match exactly. We need to provide all data the vertex shader expects, but we are allowed to have on the vertex additional data that vertex shader does not use
            // like having uv coord but the vertex shader is just a simple color one that does not use UV
            // The semantic name, such as "POSITION", is used to match vertex elements with vertex shader parameters
            m_inputElementDescriptions =
            {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
            };
        }

        void BoxDemo::SetupCube(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            SetupVertexBuffer(device, commandList);
            SetupIndexBuffer(device, commandList);
        }

        void BoxDemo::SetupVertexBuffer(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            Vertex vertices[] =
            {
                {DirectX::XMFLOAT3{-1.f, -1.f, -1.f}, DirectX::XMFLOAT4{DirectX::Colors::White}},
                {DirectX::XMFLOAT3{-1.f, 1.f, -1.f}, DirectX::XMFLOAT4{DirectX::Colors::Black}},
                {DirectX::XMFLOAT3{1.f, 1.f, -1.f}, DirectX::XMFLOAT4{DirectX::Colors::Red}},
                {DirectX::XMFLOAT3{1.f, -1.f, -1.f}, DirectX::XMFLOAT4{DirectX::Colors::Green}},
                {DirectX::XMFLOAT3{-1.f, -1.f, 1.f}, DirectX::XMFLOAT4{DirectX::Colors::Blue}},
                {DirectX::XMFLOAT3{-1.f, 1.f, 1.f}, DirectX::XMFLOAT4{DirectX::Colors::Yellow}},
                {DirectX::XMFLOAT3{1.f, 1.f, 1.f}, DirectX::XMFLOAT4{DirectX::Colors::Cyan}},
                {DirectX::XMFLOAT3{1.f, -1.f, 1.f}, DirectX::XMFLOAT4{DirectX::Colors::Magenta}},
            };

            constexpr UINT64 vertexBufferSize = 8 * sizeof(Vertex);

            m_vertexBufferGPU = nullptr;
            m_vertexBufferUploader = nullptr;

            m_vertexBufferGPU = VertexBufferUtil::CreateDefaultBuffer(&device, &commandList, vertices, vertexBufferSize, m_vertexBufferUploader);

            // In order to bind a vertex buffer to the pipeline, we need to create a vertex buffer view, which doesn't need a descriptor heap
            D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
            vertexBufferView.BufferLocation = m_vertexBufferGPU->GetGPUVirtualAddress();
            vertexBufferView.SizeInBytes = vertexBufferSize;
            vertexBufferView.StrideInBytes = sizeof(Vertex);

            // Bind the vertex view to an input slot of the pipeline (from 0 to 15) to feed vertices to the input assembler stage
            // A vertex buffer will stay bound to an input slot until we change it
            D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[1] = { vertexBufferView };
            commandList.IASetVertexBuffers(0, 1, vertexBufferViews);
        }

        void BoxDemo::SetupIndexBuffer(ID3D12Device& device, ID3D12GraphicsCommandList& commandList)
        {
            uint16_t indices[] =
            {
                // Front face
                0, 1, 2,
                0, 2, 3,

                // Back face
                4, 6, 5,
                4, 7, 6,

                // Left face
                4, 5, 1,
                4, 1, 0,

                // Right face
                3, 2, 6,
                3, 6, 7,

                // Top face
                1, 5, 6,
                1, 6, 2,

                // Bottom face
                4, 0, 3,
                4, 3, 7
            };

            constexpr UINT indexBufferSize = 36 * sizeof(uint16_t);

            m_indexBufferGPU = nullptr;
            m_indexBufferUploader = nullptr;

            m_indexBufferGPU = VertexBufferUtil::CreateDefaultBuffer(&device, &commandList, indices, indexBufferSize, m_indexBufferUploader);

            D3D12_INDEX_BUFFER_VIEW indexBufferView{};
            indexBufferView.BufferLocation = m_indexBufferGPU->GetGPUVirtualAddress();
            // For format, we should use DXGI_FORMAT_R16_UINT for 16-bit indices to reduce bandwidth.
            // Only use DXGI_FORMAT_R32_UINT if we have indexes that need the extra 32-bit range
            indexBufferView.Format = DXGI_FORMAT_R16_UINT;
            indexBufferView.SizeInBytes = indexBufferSize;

            commandList.IASetIndexBuffer(&indexBufferView);
        }
    }
}
