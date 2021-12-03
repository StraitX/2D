#include "2d/rect_renderer.hpp"
#include "core/string.hpp"
#include "core/math/trig.hpp"
#include "core/math/linear.hpp"
#include "graphics/api/command_buffer.hpp"
#include "graphics/api/gpu.hpp"
#include "graphics/api/render_pass.hpp"
#include "graphics/api/framebuffer.hpp"
#include "graphics/api/graphics_pipeline.hpp"

static const char *s_VertexShader = R"(
#version 440 core

layout(location = 0)in vec2 a_Position;
layout(location = 1)in vec2 a_TexCoords;
layout(location = 2)in vec3 a_Color;
layout(location = 3)in float a_TexIndex;

layout(location = 0)out vec3 v_Color;
layout(location = 1)out vec2 v_TexCoords;
layout(location = 2)out flat float v_TexIndex;

layout(std140, binding = 0)uniform MatricesUniform{
    mat4 u_Projection;
};

void main(){
    gl_Position = u_Projection * vec4(a_Position.xy, 0.0, 1.0);

    v_Color = a_Color;
    v_TexCoords = a_TexCoords;
    v_TexIndex = a_TexIndex;
})";

static const char *s_FragmentShader = R"(
#version 440 core

layout(location = 0)in vec3 v_Color;
layout(location = 1)in vec2 v_TexCoords;
layout(location = 2)in flat float v_TexIndex;

layout(location = 0)out vec4 f_Color;

layout(binding = 1)uniform sampler2D u_Textures[15];

void main(){
    f_Color = vec4(v_Color.rgb, 1.0) * texture(u_Textures[int(v_TexIndex)], v_TexCoords);
})";

static Array<ShaderBinding, 2> s_ShaderBindings = {
    ShaderBinding(0, 1,                              ShaderBindingType::UniformBuffer, ShaderStageBits::Vertex),
    ShaderBinding(1, RectRenderer::MaxTexturesInSet, ShaderBindingType::Texture,       ShaderStageBits::Fragment),
};

static Array<VertexAttribute, 4> s_VertexAttributes = {
    VertexAttribute::Float32x2,
    VertexAttribute::Float32x2,
    VertexAttribute::Float32x3,
    VertexAttribute::Float32x1
};

RectRenderer::Batch::Batch(){
    VerticesBuffer = Buffer::Create(sizeof(RectVertex) * MaxVerticesInBatch, BufferMemoryType::UncachedRAM, BufferUsageBits::TransferSource);
    IndicesBuffer  = Buffer::Create(sizeof(u32)        * MaxIndicesInBatch,  BufferMemoryType::UncachedRAM, BufferUsageBits::TransferSource);
    
    Vertices = VerticesBuffer->Map<RectVertex>();
    Indices  = IndicesBuffer->Map<u32>();
}

RectRenderer::Batch::~Batch(){
    delete VerticesBuffer;
    delete IndicesBuffer;
}

void RectRenderer::Batch::Reset(){
    SubmitedRectsCount = 0;
}

const Array<Vector2f, 4> RectRenderer::s_DefaultTextureCoordinates = {
    Vector2f(0.f, 0.f),
    Vector2f(1.f, 0.f),
    Vector2f(1.f, 1.f),
    Vector2f(0.f, 1.f)
};                 

RectRenderer::RectRenderer(const RenderPass *rp){
    m_FramebufferPass = rp;

    m_SetLayout = DescriptorSetLayout::Create(s_ShaderBindings);

    m_SetPool = DescriptorSetPool::Create({1, m_SetLayout});
    m_Set = m_SetPool->Alloc();

    m_Shaders[0] = Shader::Create(ShaderLang::GLSL, ShaderStageBits::Vertex,   {s_VertexShader,   String::Length(s_VertexShader)  } );
    m_Shaders[1] = Shader::Create(ShaderLang::GLSL, ShaderStageBits::Fragment, {s_FragmentShader, String::Length(s_FragmentShader)} );

    {
        GraphicsPipelineProperties props;
        props.Shaders = m_Shaders;
        props.VertexAttributes = s_VertexAttributes;
        props.Pass = m_FramebufferPass;
        props.Layout = m_SetLayout;

        m_Pipeline = GraphicsPipeline::Create(props);
    }

    m_CmdPool = CommandPool::Create();
    m_CmdBuffer = m_CmdPool->Alloc();

    m_VertexBuffer = Buffer::Create(sizeof(RectVertex) * MaxVerticesInBatch, BufferMemoryType::DynamicVRAM, BufferUsageBits::VertexBuffer | BufferUsageBits::TransferDestination);
    m_IndexBuffer  = Buffer::Create(sizeof(u32)        * MaxIndicesInBatch,  BufferMemoryType::DynamicVRAM, BufferUsageBits::IndexBuffer  | BufferUsageBits::TransferDestination);
    m_MatricesUniformBuffer = Buffer::Create(sizeof(MatricesUniform), BufferMemoryType::DynamicVRAM, BufferUsageBits::UniformBuffer | BufferUsageBits::TransferSource);


    m_WhiteTexture = Texture2D::Create(1, 1, TextureFormat::RGBA8, TextureUsageBits::TransferDst | TextureUsageBits::Sampled, TextureLayout::ShaderReadOnlyOptimal);
    m_WhiteTexture->Copy(Image(1, 1, Color::White));

    m_DefaultSampler = Sampler::Create({});

    m_DrawingFence.Signal();
}

RectRenderer::~RectRenderer(){
    m_DrawingFence.WaitFor();

    delete m_DefaultSampler;
    delete m_WhiteTexture;

    delete m_VertexBuffer;
    delete m_IndexBuffer;
    delete m_MatricesUniformBuffer;

    m_CmdPool->Free(m_CmdBuffer);
    delete m_CmdPool;

    delete m_Pipeline;

    for(auto shader: m_Shaders)
        delete shader;
    
    m_SetPool->Free(m_Set);
    delete m_SetPool;
    delete m_SetLayout;
}

Result RectRenderer::BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer, const ViewportParameters &viewport){
    m_Framebuffer = framebuffer;
    m_CurrentViewport = viewport;

    m_SemaphoreRing.Begin(wait_semaphore);
    
    m_BatcheRings.Current().Reset();

    m_MatricesUniform.u_Projection[0][0] = 2.f/framebuffer->Size().x;
    m_MatricesUniform.u_Projection[1][1] = 2.f/framebuffer->Size().y;

    //XXX Check if framebuffer matches RenderPass
    return Result::Success;
}

Result RectRenderer::BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer){
    ViewportParameters default_params;
    default_params.ViewportOffset = {0.f, 0.f};
    default_params.ViewportSize = Vector2f(framebuffer->Size());
    return BeginDrawing(wait_semaphore, framebuffer, default_params);
}

void RectRenderer::EndDrawing(const Semaphore *signal_semaphore){
    Flush(m_SemaphoreRing.Current(), signal_semaphore);

    m_SemaphoreRing.End();
}

void RectRenderer::DrawRect(Vector2s position, Vector2s size, Vector2s origin, float angle, Color color, Texture2D *texture, const Array<Vector2f, 4> &texture_coords){
    if(m_BatcheRings.Current().IsGeometryFull()
    || !m_BatcheRings.Current().HasTexture(texture) && m_BatcheRings.Current().IsTexturesFull()){
        Flush();
    }

    Batch &batch = m_BatcheRings.Current();

    auto texture_it = batch.Textures.Find(texture);

    if(texture_it == batch.Textures.end())
        batch.Textures.Add(texture);// texture_it now points at newely inserted texture
    
    float texture_index = texture_it - batch.Textures.begin();

    size_t base_vertex = batch.SubmitedRectsCount * 4;
    size_t base_index  = batch.SubmitedRectsCount * 6;

    Array<Vector2f, 4>rect_vertices = {
        Vector2f(0,      0         ) - Vector2f(origin),
        Vector2f(size.x, 0         ) - Vector2f(origin),
        Vector2f(size.x, 0 + size.y) - Vector2f(origin),
        Vector2f(0,      0 + size.y) - Vector2f(origin)
    };

    Rotate(rect_vertices, angle);

    Vector2f offset = Vector2f(m_Framebuffer->Size()/2u) - m_CurrentViewport.Offset;

    for(auto &vertex: rect_vertices){
        vertex += Vector2f(position);
        vertex *= m_CurrentViewport.Scale;
        vertex -= offset;
    }

    batch.Vertices[base_vertex + 0] = {rect_vertices[0], texture_coords[0], Vector3f(color.R, color.G, color.B), texture_index};
    batch.Vertices[base_vertex + 1] = {rect_vertices[1], texture_coords[1], Vector3f(color.R, color.G, color.B), texture_index};
    batch.Vertices[base_vertex + 2] = {rect_vertices[2], texture_coords[2], Vector3f(color.R, color.G, color.B), texture_index};
    batch.Vertices[base_vertex + 3] = {rect_vertices[3], texture_coords[3], Vector3f(color.R, color.G, color.B), texture_index};

    batch.Indices[base_index + 0] = batch.SubmitedRectsCount * 4 + 0;
    batch.Indices[base_index + 1] = batch.SubmitedRectsCount * 4 + 1;
    batch.Indices[base_index + 2] = batch.SubmitedRectsCount * 4 + 2;

    batch.Indices[base_index + 3] = batch.SubmitedRectsCount * 4 + 2;
    batch.Indices[base_index + 4] = batch.SubmitedRectsCount * 4 + 3;
    batch.Indices[base_index + 5] = batch.SubmitedRectsCount * 4 + 0;

    batch.SubmitedRectsCount++;
}
void RectRenderer::Flush(const Semaphore *wait_semaphore, const Semaphore *signal_semaphore){
    m_DrawingFence.WaitAndReset();

    Batch &batch = m_BatcheRings.Current();

    m_MatricesUniformBuffer->Copy(&m_MatricesUniform, sizeof(m_MatricesUniform));

    m_Set->UpdateUniformBinding(0, 0, m_MatricesUniformBuffer);
    for(size_t i = 0; i<batch.Textures.Size(); i++)
        m_Set->UpdateTextureBinding(1, i, batch.Textures[i], m_DefaultSampler);

    m_CmdBuffer->Reset();
    m_CmdBuffer->Begin();
    {
        Vector2u fb_size = m_Framebuffer->Size();
        m_CmdBuffer->Copy(batch.VerticesBuffer, m_VertexBuffer, batch.SubmitedRectsCount * 4 * sizeof(RectVertex));
        m_CmdBuffer->Copy(batch.IndicesBuffer, m_IndexBuffer, batch.SubmitedRectsCount * 6 * sizeof(u32));
        m_CmdBuffer->SetScissor (m_CurrentViewport.ViewportOffset.x, m_CurrentViewport.ViewportOffset.y, m_CurrentViewport.ViewportSize.x, m_CurrentViewport.ViewportSize.y);
        m_CmdBuffer->SetViewport(m_CurrentViewport.ViewportOffset.x, m_CurrentViewport.ViewportOffset.y, m_CurrentViewport.ViewportSize.x, m_CurrentViewport.ViewportSize.y);
        m_CmdBuffer->Bind(m_Pipeline);
        m_CmdBuffer->Bind(m_Set);
        m_CmdBuffer->BeginRenderPass(m_FramebufferPass, m_Framebuffer);
        m_CmdBuffer->BindVertexBuffer(m_VertexBuffer);
        m_CmdBuffer->BindIndexBuffer(m_IndexBuffer, IndicesType::Uint32);
        m_CmdBuffer->DrawIndexed(batch.SubmitedRectsCount * 6);
        m_CmdBuffer->EndRenderPass();
    }
    m_CmdBuffer->End();

    GPU::Execute(m_CmdBuffer, *wait_semaphore, *signal_semaphore, m_DrawingFence);

    batch.Reset();
    m_BatcheRings.Advance();
}

void RectRenderer::Flush() {
    Flush(m_SemaphoreRing.Current(), m_SemaphoreRing.Next());
    m_SemaphoreRing.Advance();
}

void RectRenderer::Rotate(Span<Vector2f> vertices, float degrees){
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