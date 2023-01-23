#include "2d/rect_renderer.hpp"
#include "core/string.hpp"
#include "core/ranges/algorithm.hpp"
#include "core/math/functions.hpp"
#include "core/math/linear.hpp"
#include "graphics/api/gpu.hpp"
#include "graphics/api/render_pass.hpp"
#include "graphics/api/framebuffer.hpp"


static const char *s_VertexShader = 
    #include "shaders/rect_renderer.vert.glsl"
;

static const char *s_FragmentShader = 
    #include "shaders/rect_renderer.frag.glsl"
;

static Array<VertexAttribute, 4> s_VertexAttributes = {
    VertexAttribute::Float32x2,
    VertexAttribute::Float32x2,
    VertexAttribute::UNorm8x4,
    VertexAttribute::Float32x1
};

RectRenderer::Batch::Batch(u16 primitives_count):
    Vertices(new RectVertex[(size_t)primitives_count * 4]),
    Indices(new u16[(size_t)primitives_count * 6]),
    VertexBuffer(
        Buffer::Create(sizeof(RectVertex) * primitives_count * 4, BufferMemoryType::DynamicVRAM, BufferUsageBits::VertexBuffer | BufferUsageBits::TransferDestination)
    ),
    IndexBuffer(
        Buffer::Create(sizeof(u16) * primitives_count * 6, BufferMemoryType::DynamicVRAM, BufferUsageBits::IndexBuffer | BufferUsageBits::TransferDestination)
    )
{}


bool RectRenderer::Batch::IsFull()const {
    return Textures.Size() == Textures.Capacity() || SubmitedPrimitives == MaxPrimitivesCount();
}

static void Rotate(Span<Vector2f> vertices, float degrees){
    float radians = Math::Rad(degrees);

    Vector2f mat[2];
    mat[0][0] = Math::Cos(radians);
    mat[0][1] =-Math::Sin(radians);
    mat[1][0] = Math::Sin(radians);
    mat[1][1] = Math::Cos(radians);

    for(auto &vertex: vertices){
        vertex = Vector2f(
            Dot(mat[0], vertex),
            Dot(mat[1], vertex)
        );
    }
}

void RectRenderer::Batch::PushRect(Vector2f position, Vector2f size, Vector2f origin, float angle, Color color, Texture2D* texture, const Array<Vector2f, 4>& texture_coords) {
     
    size_t texture_index = Textures | IndexOf(texture);

    if (texture_index == -1) {
        texture_index = Textures.Size();
        Textures.Add(texture);
    }

    size_t base_vertex = SubmitedPrimitives * 4;
    size_t base_index  = SubmitedPrimitives * 6;

    Array<Vector2f, 4> rect_vertices = {
        Vector2f(0,      0         ) - Vector2f(origin),
        Vector2f(size.x, 0         ) - Vector2f(origin),
        Vector2f(size.x, 0 + size.y) - Vector2f(origin),
        Vector2f(0,      0 + size.y) - Vector2f(origin)
    };

    Rotate(rect_vertices, angle);

    Vertices[base_vertex + 0] = {rect_vertices[0] + position, texture_coords[0], color.RGBA8(), (float)texture_index};
    Vertices[base_vertex + 1] = {rect_vertices[1] + position, texture_coords[1], color.RGBA8(), (float)texture_index};
    Vertices[base_vertex + 2] = {rect_vertices[2] + position, texture_coords[2], color.RGBA8(), (float)texture_index};
    Vertices[base_vertex + 3] = {rect_vertices[3] + position, texture_coords[3], color.RGBA8(), (float)texture_index};

    Indices[base_index + 0] = SubmitedPrimitives * 4 + 0;
    Indices[base_index + 1] = SubmitedPrimitives * 4 + 1;
    Indices[base_index + 2] = SubmitedPrimitives * 4 + 2;

    Indices[base_index + 3] = SubmitedPrimitives * 4 + 2;
    Indices[base_index + 4] = SubmitedPrimitives * 4 + 3;
    Indices[base_index + 5] = SubmitedPrimitives * 4 + 0;

    SubmitedPrimitives++;
}

u16 RectRenderer::Batch::MaxPrimitivesCount()const {
    return VertexBuffer->Size() / sizeof(RectVertex) / 4;
}

const Array<Vector2f, 4> RectRenderer::s_DefaultTextureCoordinates = {
    Vector2f(0.f, 0.f),
    Vector2f(1.f, 0.f),
    Vector2f(1.f, 1.f),
    Vector2f(0.f, 1.f)
};                 

RectRenderer::RectRenderer(const RenderPass *rp):
    m_SetLayout(
        DescriptorSetLayout::Create({
            ShaderBinding(0, 1,                                       ShaderBindingType::UniformBuffer, ShaderStageBits::Vertex),
            ShaderBinding(1, RectRenderer::Batch::MaxTexturesInBatch, ShaderBindingType::Texture,       ShaderStageBits::Fragment)
        })
    ),
    m_Pipeline(nullptr)
{
    m_FramebufferPass = rp;
    
    Array<const Shader*, 2> shaders;
    shaders[0] = Shader::Create(ShaderStageBits::Vertex,   {s_VertexShader,   String::Length(s_VertexShader)  } );
    shaders[1] = Shader::Create(ShaderStageBits::Fragment, {s_FragmentShader, String::Length(s_FragmentShader)} );

    {
        GraphicsPipelineProperties props;
        props.Shaders = shaders;
        props.VertexAttributes = s_VertexAttributes;
        props.Pass = m_FramebufferPass;
        props.Layout = m_SetLayout.Get();

        m_Pipeline = GraphicsPipeline::Create(props);
    }
}

//m_MatricesUniform.u_Projection[0][0] = 2.f/framebuffer->Size().x;
//m_MatricesUniform.u_Projection[1][1] = 2.f/framebuffer->Size().y;

void RectRenderer::DrawRect(Vector2f position, Vector2f size, Vector2f origin, float angle, Color color, Texture2D *texture, const Array<Vector2f, 4> &texture_coords){
    static constexpr size_t MaxPrimitivesInBatch = 6000;
    if (!m_Batches.Size() || m_Batches.Last().IsFull())
        m_Batches.Add({ MaxPrimitivesInBatch });

    m_Batches.Last().PushRect(position, size, origin, angle, color, texture, texture_coords);
}

void RectRenderer::CmdRender(CommandBuffer* cmd_buffer, const Framebuffer* fb, const ViewportParameters& viewport) {
    m_SetPool.NextFrame();
    
    const auto vp = viewport.ViewportSize;
    Matrix4f projection{
        {2.f / vp.x, 0,                0, 0},
        {0,          2.f/vp.y,         0, 0},
        {0,          0,                1, 0},
        {0,          0,                0, 1}
    };

    cmd_buffer->SetScissor(0, 0, fb->Size().x, fb->Size().y);
    cmd_buffer->SetViewport(0, 0, fb->Size().x, fb->Size().y);

    cmd_buffer->Copy({projection}, m_MatricesUniformBuffer);    
    cmd_buffer->Bind(m_Pipeline.Get());
    cmd_buffer->BeginRenderPass(m_FramebufferPass, fb);
    for (Batch& batch : m_Batches) {
        auto* set = m_SetPool.Alloc();
        batch.VertexBuffer->Copy(batch.Vertices.Get(), batch.VertexBuffer->Size());
        batch.IndexBuffer->Copy(batch.Indices.Get(), batch.IndexBuffer->Size());
        set->UpdateUniformBinding(0, 0, m_MatricesUniformBuffer);

        for (size_t i = 0; i < batch.Textures.Size(); i++)
            set->UpdateTextureBinding(1, i, batch.Textures[i], m_DefaultSampler.Get());
        
        cmd_buffer->Bind(set);
        cmd_buffer->BindVertexBuffer(batch.VertexBuffer.Get());
        cmd_buffer->BindIndexBuffer(batch.IndexBuffer.Get(), IndicesType::Uint16);
        cmd_buffer->DrawIndexed(batch.SubmitedPrimitives * 6);
    }
    cmd_buffer->EndRenderPass();

    for (auto& batch : m_Batches)
        batch.Reset();
}
