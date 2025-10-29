#include "MeshGeometry.h"

namespace Studies
{
    D3D12_VERTEX_BUFFER_VIEW MeshGeometry::GetVertexBufferView() const
    {
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
        vertexBufferView.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
        vertexBufferView.StrideInBytes = VertexByteStride;
        vertexBufferView.SizeInBytes = VertexBufferByteSize;

        return vertexBufferView;
    }

    D3D12_INDEX_BUFFER_VIEW MeshGeometry::GetIndexBufferView() const
    {
        D3D12_INDEX_BUFFER_VIEW indexBufferView{};
        indexBufferView.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
        indexBufferView.Format = IndexFormat;
        indexBufferView.SizeInBytes = IndexBufferByteSize;

        return indexBufferView;
    }
}
