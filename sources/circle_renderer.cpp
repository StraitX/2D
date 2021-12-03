#include "2d/circle_renderer.hpp"
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
layout(location = 1)in vec2 a_Center;
layout(location = 2)in vec4 a_Color;
layout(location = 3)in float a_Radius;

layout(location = 0)out vec4 v_Color;
layout(location = 1)out vec2 v_Position;
layout(location = 2)out vec2 v_Center;
layout(location = 3)out flat float v_Radius;

layout(std140, binding = 0)uniform MatricesUniform{
    mat4 u_Projection;
};

void main(){
    gl_Position = u_Projection * vec4(a_Position.xy, 0.0, 1.0);

    v_Color = a_Color;
    v_Position = a_Position;
    v_Center = a_Center;
    v_Radius = a_Radius;
})";

static const char *s_FragmentShader = R"(
#version 440 core

layout(location = 0)in vec4 v_Color;
layout(location = 1)in vec2 v_Position;
layout(location = 2)in vec2 v_Center;
layout(location = 3)in flat float v_Radius;

layout(location = 0)out vec4 f_Color;

void main(){

    if(length(v_Center) > v_Radius)
        discard;
    f_Color = v_Color;
})";

static Array<ShaderBinding, 1> s_ShaderBindings = {
        ShaderBinding(0, 1,ShaderBindingType::UniformBuffer, ShaderStageBits::Vertex)
};

static Array<VertexAttribute, 4> s_VertexAttributes = {
        VertexAttribute::Float32x2,
        VertexAttribute::Float32x2,
        VertexAttribute::UNorm8x4,
        VertexAttribute::Float32x1,
};

CircleRenderer::Batch::Batch(){
    VerticesBuffer = Buffer::Create(sizeof(CircleVertex) * MaxVerticesInBatch, BufferMemoryType::UncachedRAM, BufferUsageBits::TransferSource);
    IndicesBuffer  = Buffer::Create(sizeof(u32)        * MaxIndicesInBatch,  BufferMemoryType::UncachedRAM, BufferUsageBits::TransferSource);

    Vertices = VerticesBuffer->Map<CircleVertex>();
    Indices  = IndicesBuffer->Map<u32>();
}

CircleRenderer::Batch::~Batch(){
    delete VerticesBuffer;
    delete IndicesBuffer;
}

void CircleRenderer::Batch::Reset(){
    SubmitedCirclesCount = 0;
}

CircleRenderer::CircleRenderer(const RenderPass *rp){
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

    m_VertexBuffer = Buffer::Create(sizeof(CircleVertex) * MaxVerticesInBatch, BufferMemoryType::DynamicVRAM, BufferUsageBits::VertexBuffer | BufferUsageBits::TransferDestination);
    m_IndexBuffer  = Buffer::Create(sizeof(u32)        * MaxIndicesInBatch,  BufferMemoryType::DynamicVRAM, BufferUsageBits::IndexBuffer  | BufferUsageBits::TransferDestination);
    m_MatricesUniformBuffer = Buffer::Create(sizeof(MatricesUniform), BufferMemoryType::DynamicVRAM, BufferUsageBits::UniformBuffer | BufferUsageBits::TransferSource);

    m_Set->UpdateUniformBinding(0, 0, m_MatricesUniformBuffer);

    m_DrawingFence.Signal();
}

CircleRenderer::~CircleRenderer(){
    m_DrawingFence.WaitFor();

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

Result CircleRenderer::BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer, const ViewportParameters &viewport){
    m_Framebuffer = framebuffer;
    m_CurrentViewport = viewport;

    m_SemaphoreRing.Begin(wait_semaphore);

    m_BatcheRings.Current().Reset();

    m_MatricesUniform.u_Projection[0][0] = 2.f/framebuffer->Size().x;
    m_MatricesUniform.u_Projection[1][1] = 2.f/framebuffer->Size().y;

    //XXX Check if framebuffer matches RenderPass
    return Result::Success;
}

Result CircleRenderer::BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer){
    ViewportParameters default_params;
    default_params.ViewportOffset = {0.f, 0.f};
    default_params.ViewportSize = Vector2f(framebuffer->Size());
    return BeginDrawing(wait_semaphore, framebuffer, default_params);
}

void CircleRenderer::EndDrawing(const Semaphore *signal_semaphore){
    Flush(m_SemaphoreRing.Current(), signal_semaphore);

    m_SemaphoreRing.End();
}


void CircleRenderer::DrawCircle(Vector2s center, float radius, Color color){
    if(m_BatcheRings.Current().IsGeometryFull())
        Flush();

    Batch &batch = m_BatcheRings.Current();

    size_t base_vertex = batch.SubmitedCirclesCount * 4;
    size_t base_index  = batch.SubmitedCirclesCount * 6;

    Array<Vector2f, 4> vertices = {
            Vector2f(center) + Vector2f(-radius,-radius),
            Vector2f(center) + Vector2f( radius,-radius),
            Vector2f(center) + Vector2f( radius, radius),
            Vector2f(center) + Vector2f(-radius, radius)
    };

    Vector2f offset = Vector2f(m_Framebuffer->Size()/2u) - m_CurrentViewport.Offset;

    for(auto &vertex: vertices){
        vertex *= m_CurrentViewport.Scale;
        vertex -= offset;
    }

    batch.Vertices[base_vertex + 0] = {vertices[0], Vector2f(-radius,-radius), color.RGBA8(), radius};
    batch.Vertices[base_vertex + 1] = {vertices[1], Vector2f( radius,-radius), color.RGBA8(), radius};
    batch.Vertices[base_vertex + 2] = {vertices[2], Vector2f( radius, radius), color.RGBA8(), radius};
    batch.Vertices[base_vertex + 3] = {vertices[3], Vector2f(-radius, radius), color.RGBA8(), radius};

    batch.Indices[base_index + 0] = batch.SubmitedCirclesCount * 4 + 0;
    batch.Indices[base_index + 1] = batch.SubmitedCirclesCount * 4 + 1;
    batch.Indices[base_index + 2] = batch.SubmitedCirclesCount * 4 + 2;

    batch.Indices[base_index + 3] = batch.SubmitedCirclesCount * 4 + 2;
    batch.Indices[base_index + 4] = batch.SubmitedCirclesCount * 4 + 3;
    batch.Indices[base_index + 5] = batch.SubmitedCirclesCount * 4 + 0;

    batch.SubmitedCirclesCount++;
}

void CircleRenderer::Flush(const Semaphore *wait_semaphore, const Semaphore *signal_semaphore){
    m_DrawingFence.WaitAndReset();

    Batch &batch = m_BatcheRings.Current();

    m_MatricesUniformBuffer->Copy(&m_MatricesUniform, sizeof(m_MatricesUniform));

    m_CmdBuffer->Reset();
    m_CmdBuffer->Begin();

    if(batch.SubmitedCirclesCount){
        m_CmdBuffer->Copy(batch.VerticesBuffer, m_VertexBuffer, batch.SubmitedCirclesCount * 4 * sizeof(CircleVertex));
        m_CmdBuffer->Copy(batch.IndicesBuffer, m_IndexBuffer, batch.SubmitedCirclesCount * 6 * sizeof(u32));
        m_CmdBuffer->SetScissor (m_CurrentViewport.ViewportOffset.x, m_CurrentViewport.ViewportOffset.y, m_CurrentViewport.ViewportSize.x, m_CurrentViewport.ViewportSize.y);
        m_CmdBuffer->SetViewport(m_CurrentViewport.ViewportOffset.x, m_CurrentViewport.ViewportOffset.y, m_CurrentViewport.ViewportSize.x, m_CurrentViewport.ViewportSize.y);
        m_CmdBuffer->Bind(m_Pipeline);
        m_CmdBuffer->Bind(m_Set);
        m_CmdBuffer->BeginRenderPass(m_FramebufferPass, m_Framebuffer);
            m_CmdBuffer->BindVertexBuffer(m_VertexBuffer);
            m_CmdBuffer->BindIndexBuffer(m_IndexBuffer, IndicesType::Uint32);
            m_CmdBuffer->DrawIndexed(batch.SubmitedCirclesCount * 6);
        m_CmdBuffer->EndRenderPass();
    }

    m_CmdBuffer->End();

    GPU::Execute(m_CmdBuffer, *wait_semaphore, *signal_semaphore, m_DrawingFence);

    batch.Reset();
    m_BatcheRings.Advance();
}

void CircleRenderer::Flush() {
    Flush(m_SemaphoreRing.Current(), m_SemaphoreRing.Next());
    m_SemaphoreRing.Advance();
}