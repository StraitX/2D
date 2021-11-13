#include "2d/rect_renderer.hpp"
#include "core/string.hpp"
#include "core/array.hpp"
#include "graphics/api/graphics_api.hpp"
#include "graphics/api/swapchain.hpp"
#include "graphics/api/command_buffer.hpp"
#include "graphics/api/semaphore.hpp"
#include "graphics/api/gpu.hpp"
#include "graphics/api/render_pass.hpp"
#include "graphics/api/framebuffer.hpp"
#include "graphics/api/shader.hpp"
#include "graphics/api/graphics_pipeline.hpp"
#include "graphics/api/buffer.hpp"
#include "graphics/api/descriptor_set.hpp"

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

//layout(binding = 1)uniform sampler2D u_Textures[15];

void main(){
    f_Color = vec4(v_Color.rgb, 1.0);// * texture(u_Textures[int(v_TexIndex)], v_TexCoords);
})";

static Array<ShaderBinding, 1> s_ShaderBindings = {
    ShaderBinding(0, 1, ShaderBindingType::UniformBuffer, ShaderStageBits::Vertex)
};

static Array<VertexAttribute, 4> s_VertexAttributes = {
    VertexAttribute::Float32x2,
    VertexAttribute::Float32x2,
    VertexAttribute::Float32x3,
    VertexAttribute::Float32x1
};

void RectRenderer::BatchData::Begin(const Framebuffer * fb){
    SX_CORE_ASSERT(TargetFramebuffer == nullptr, "RectRenderer: Batch should be ended");
    SubmitedRectsCount = 0;
    TargetFramebuffer = fb;
}

void RectRenderer::BatchData::End(){
    TargetFramebuffer = nullptr;
}

RectRenderer::SemaphoreRing::SemaphoreRing(){
    FullRing[1] = &LoopingPart[0];
    FullRing[2] = &LoopingPart[1];
}

void RectRenderer::SemaphoreRing::Begin(const Semaphore *first){
    SX_CORE_ASSERT(FullRing[0] == nullptr, "RectRenderer: SemaphoreRing should be ended");
    Index = 0;
    FullRing[0] = first;
}

void RectRenderer::SemaphoreRing::End(){
    FullRing[0] = nullptr;
}

const Semaphore *RectRenderer::SemaphoreRing::Current(){
    return FullRing[Index];
}

const Semaphore *RectRenderer::SemaphoreRing::Next(){
    return FullRing[NextIndex()];
}

void RectRenderer::SemaphoreRing::Advance(){
    Index = NextIndex();
}

u32 RectRenderer::SemaphoreRing::NextIndex(){
    u32 next_index = Index + 1;
    if(next_index == 3)
        next_index = 1;
    return next_index;
}

Result RectRenderer::Initialize(const RenderPass *rp){
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

    m_CurrentBatch.Vertices = new RectVertex[MaxVerticesInBatch];
    m_CurrentBatch.Indices  = new u32[MaxIndicesInBatch];

    m_SemaphoreRing.Construct();
    m_DrawingFence = new Fence;

    m_VertexBuffer = Buffer::Create(sizeof(RectVertex) * MaxVerticesInBatch, BufferMemoryType::DynamicVRAM, BufferUsageBits::VertexBuffer | BufferUsageBits::TransferDestination);
    m_IndexBuffer  = Buffer::Create(sizeof(u32)        * MaxIndicesInBatch,  BufferMemoryType::DynamicVRAM, BufferUsageBits::IndexBuffer  | BufferUsageBits::TransferDestination);
    m_MatricesUniformBuffer = Buffer::Create(sizeof(MatricesUniform), BufferMemoryType::DynamicVRAM, BufferUsageBits::UniformBuffer | BufferUsageBits::TransferSource);

    m_Set->UpdateUniformBinding(0, 0, m_MatricesUniformBuffer);

    m_DrawingFence->Signal();

    return Result::Success;
}

void RectRenderer::Finalize(){
    m_DrawingFence->WaitFor();

    delete m_VertexBuffer;
    delete m_IndexBuffer;
    delete m_MatricesUniformBuffer;

    delete[] m_CurrentBatch.Vertices;
    delete[] m_CurrentBatch.Indices;

    delete m_DrawingFence;
    m_SemaphoreRing.Destruct();

    m_CmdPool->Free(m_CmdBuffer);
    delete m_CmdPool;

    delete m_Pipeline;

    for(auto shader: m_Shaders)
        delete shader;
    
    m_SetPool->Free(m_Set);
    delete m_SetPool;
    delete m_SetLayout;
}

bool RectRenderer::IsInitialized()const{
    // XXX: find a better way to validate renderer
    return m_Pipeline != nullptr;
}

Result RectRenderer::BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer){
    m_SemaphoreRing->Begin(wait_semaphore);
    m_CurrentBatch.Begin(framebuffer);

    m_MatricesUniform.u_Projection[0][0] = 2.f/framebuffer->Size().x;
    m_MatricesUniform.u_Projection[1][1] = 2.f/framebuffer->Size().y;

    //XXX Check if framebuffer matches RenderPass
    return Result::Success;
}

void RectRenderer::EndDrawing(const Semaphore *signal_semaphore){
    Flush(m_SemaphoreRing->Current(), signal_semaphore);

    m_SemaphoreRing->End();
    m_CurrentBatch.End();
}

void RectRenderer::Flush(const Semaphore *wait_semaphore, const Semaphore *signal_semaphore){
    m_DrawingFence->WaitAndReset();

    m_VertexBuffer->Copy(m_CurrentBatch.Vertices, sizeof(RectVertex) * 4 * m_CurrentBatch.SubmitedRectsCount);
    m_IndexBuffer ->Copy(m_CurrentBatch.Indices,  sizeof(u32)        * 6 * m_CurrentBatch.SubmitedRectsCount);
    m_MatricesUniformBuffer->Copy(&m_MatricesUniform, sizeof(m_MatricesUniform));

    m_CmdBuffer->Reset();
    m_CmdBuffer->Begin();
    {
        Vector2u fb_size = m_CurrentBatch.TargetFramebuffer->Size();
        m_CmdBuffer->SetScissor(0, 0, fb_size.x, fb_size.y);
        m_CmdBuffer->SetViewport(0, 0, fb_size.x, fb_size.y);
        m_CmdBuffer->Bind(m_Pipeline);
        m_CmdBuffer->Bind(m_Set);
        m_CmdBuffer->BeginRenderPass(m_FramebufferPass, m_CurrentBatch.TargetFramebuffer);
            m_CmdBuffer->BindVertexBuffer(m_VertexBuffer);
            m_CmdBuffer->BindIndexBuffer(m_IndexBuffer, IndicesType::Uint32);
            m_CmdBuffer->DrawIndexed(m_CurrentBatch.SubmitedRectsCount * 6);
        m_CmdBuffer->EndRenderPass();
    }
    m_CmdBuffer->End();

    GPU::Execute(m_CmdBuffer, *wait_semaphore, *signal_semaphore, *m_DrawingFence);
    

    m_CurrentBatch.SubmitedRectsCount = 0;
}

void RectRenderer::DrawRect(Vector2s position, Vector2s size, Color color){
    if(m_CurrentBatch.SubmitedRectsCount == MaxRectsInBatch){
        Flush(m_SemaphoreRing->Current(), m_SemaphoreRing->Next());
        m_SemaphoreRing->Advance();
    }

    size_t base_vertex = m_CurrentBatch.SubmitedRectsCount * 4;
    size_t base_index  = m_CurrentBatch.SubmitedRectsCount * 6;

    Vector2f offset = Vector2f(m_CurrentBatch.TargetFramebuffer->Size()/2u);
    m_CurrentBatch.Vertices[base_vertex + 0] = {Vector2f(position.x,          position.y         ) - offset, Vector2f(0, 0), Vector3f(color.R, color.G, color.B), 0.f};
    m_CurrentBatch.Vertices[base_vertex + 1] = {Vector2f(position.x + size.x, position.y         ) - offset, Vector2f(1, 0), Vector3f(color.R, color.G, color.B), 0.f};
    m_CurrentBatch.Vertices[base_vertex + 2] = {Vector2f(position.x + size.x, position.y + size.y) - offset, Vector2f(1, 1), Vector3f(color.R, color.G, color.B), 0.f};
    m_CurrentBatch.Vertices[base_vertex + 3] = {Vector2f(position.x,          position.y + size.y) - offset, Vector2f(0, 1), Vector3f(color.R, color.G, color.B), 0.f};

    m_CurrentBatch.Indices[base_index + 0] = m_CurrentBatch.SubmitedRectsCount * 4 + 0;
    m_CurrentBatch.Indices[base_index + 1] = m_CurrentBatch.SubmitedRectsCount * 4 + 1;
    m_CurrentBatch.Indices[base_index + 2] = m_CurrentBatch.SubmitedRectsCount * 4 + 2;

    m_CurrentBatch.Indices[base_index + 3] = m_CurrentBatch.SubmitedRectsCount * 4 + 2;
    m_CurrentBatch.Indices[base_index + 4] = m_CurrentBatch.SubmitedRectsCount * 4 + 3;
    m_CurrentBatch.Indices[base_index + 5] = m_CurrentBatch.SubmitedRectsCount * 4 + 0;

    m_CurrentBatch.SubmitedRectsCount++;
}