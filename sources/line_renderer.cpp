#include "2d/line_renderer.hpp"
#include "core/string.hpp"
#include "graphics/api/command_buffer.hpp"
#include "graphics/api/gpu.hpp"
#include "graphics/api/render_pass.hpp"
#include "graphics/api/framebuffer.hpp"
#include "graphics/api/graphics_pipeline.hpp"

static const char *s_VertexShader = 
    #include "shaders/line_renderer.vert.glsl"
;

static const char *s_FragmentShader = 
    #include "shaders/line_renderer.frag.glsl"
;

static Array<ShaderBinding, 1> s_ShaderBindings = {
        ShaderBinding(0, 1,                              ShaderBindingType::UniformBuffer, ShaderStageBits::Vertex)
};

static Array<VertexAttribute, 2> s_VertexAttributes = {
        VertexAttribute::Float32x2,
        VertexAttribute::UNorm8x4
};

LineRenderer::Batch::Batch(){
    VerticesBuffer = Buffer::Create(sizeof(LineVertex) * MaxVerticesInBatch, BufferMemoryType::UncachedRAM, BufferUsageBits::TransferSource);
    IndicesBuffer  = Buffer::Create(sizeof(u32)        * MaxIndicesInBatch,  BufferMemoryType::UncachedRAM, BufferUsageBits::TransferSource);

    Vertices = VerticesBuffer->Map<LineVertex>();
    Indices  = IndicesBuffer->Map<u32>();
}

LineRenderer::Batch::~Batch(){
    delete VerticesBuffer;
    delete IndicesBuffer;
}

void LineRenderer::Batch::Reset(){
    SubmitedVerticesCount = 0;
    SubmitedIndicesCount  = 0;
    LineWidth = InvalidLineWidth;
}

LineRenderer::LineRenderer(const RenderPass *rp){
    m_FramebufferPass = rp;

    m_SetLayout = DescriptorSetLayout::Create(s_ShaderBindings);

    m_SetPool = DescriptorSetPool::Create({1, m_SetLayout});
    m_Set = m_SetPool->Alloc();

    m_Shaders[0] = Shader::Create(ShaderStageBits::Vertex,   {s_VertexShader,   String::Length(s_VertexShader)  } );
    m_Shaders[1] = Shader::Create(ShaderStageBits::Fragment, {s_FragmentShader, String::Length(s_FragmentShader)} );

    {
        GraphicsPipelineProperties props;
        props.PrimitivesTopology = PrimitivesTopology::LinesStrip;
        props.PrimitiveRestartEnable = true;
        props.Shaders = m_Shaders;
        props.VertexAttributes = s_VertexAttributes;
        props.Pass = m_FramebufferPass;
        props.Layout = m_SetLayout;

        m_Pipeline = GraphicsPipeline::Create(props);
    }

    m_CmdPool = CommandPool::Create();
    m_CmdBuffer = m_CmdPool->Alloc();

    m_VertexBuffer = Buffer::Create(sizeof(LineVertex) * MaxVerticesInBatch, BufferMemoryType::DynamicVRAM, BufferUsageBits::VertexBuffer | BufferUsageBits::TransferDestination);
    m_IndexBuffer  = Buffer::Create(sizeof(u32)        * MaxIndicesInBatch,  BufferMemoryType::DynamicVRAM, BufferUsageBits::IndexBuffer  | BufferUsageBits::TransferDestination);
    m_MatricesUniformBuffer = Buffer::Create(sizeof(MatricesUniform), BufferMemoryType::DynamicVRAM, BufferUsageBits::UniformBuffer | BufferUsageBits::TransferSource);

    m_Set->UpdateUniformBinding(0, 0, m_MatricesUniformBuffer);

    m_DrawingFence.Signal();
}

LineRenderer::~LineRenderer(){
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

Result LineRenderer::BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer, const ViewportParameters &viewport){
    m_Framebuffer = framebuffer;
    m_CurrentViewport = viewport;

    m_SemaphoreRing.Begin(wait_semaphore);

    m_BatchRing.Current().Reset();

    m_MatricesUniform.u_Projection[0][0] = 2.f/framebuffer->Size().x;
    m_MatricesUniform.u_Projection[1][1] = 2.f/framebuffer->Size().y;

    //XXX Check if framebuffer matches RenderPass
    return Result::Success;
}

Result LineRenderer::BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer){
    ViewportParameters default_params;
    default_params.ViewportOffset = {0.f, 0.f};
    default_params.ViewportSize = Vector2f(framebuffer->Size());
    return BeginDrawing(wait_semaphore, framebuffer, default_params);
}

void LineRenderer::EndDrawing(const Semaphore *signal_semaphore){
    Flush(m_SemaphoreRing.Current(), signal_semaphore);

    m_SemaphoreRing.End();
}

void LineRenderer::DrawLines(ConstSpan<Vector2s> points, Color color, u32 width){
    if(m_BatchRing.Current().IsGeometryFull()
       || m_BatchRing.Current().LineWidth != InvalidLineWidth && m_BatchRing.Current().LineWidth != width){
        Flush();
    }
    Batch &batch = m_BatchRing.Current();

    batch.LineWidth = width;

    Vector2f offset = Vector2f(m_Framebuffer->Size()/2u) - m_CurrentViewport.Offset;

    for(const Vector2s &point: points){
        LineVertex vertex;
        vertex.a_Position = (Vector2f(point) - offset) * m_CurrentViewport.Scale;
        vertex.a_Color = color.RGBA8();

        batch.Vertices[batch.SubmitedVerticesCount] = vertex;
        batch.Indices[batch.SubmitedIndicesCount] = (u32)batch.SubmitedVerticesCount;

        batch.SubmitedVerticesCount++;
        batch.SubmitedIndicesCount++;
    }

    batch.Indices[batch.SubmitedIndicesCount++] = 0xFFFFFFFF;
}

void LineRenderer::Flush(const Semaphore *wait_semaphore, const Semaphore *signal_semaphore){
    m_DrawingFence.WaitAndReset();

    Batch &batch = m_BatchRing.Current();

    SX_CORE_ASSERT(batch.LineWidth != InvalidLineWidth, "Can't flush batch with invalid line width");

    m_MatricesUniformBuffer->Copy(&m_MatricesUniform, sizeof(m_MatricesUniform));


    m_CmdBuffer->Reset();
    m_CmdBuffer->Begin();

    if(batch.SubmitedIndicesCount && batch.SubmitedVerticesCount){
        m_CmdBuffer->Copy(batch.VerticesBuffer, m_VertexBuffer, batch.SubmitedVerticesCount * sizeof(LineVertex));
        m_CmdBuffer->Copy(batch.IndicesBuffer, m_IndexBuffer, batch.SubmitedIndicesCount * sizeof(u32));
        m_CmdBuffer->SetScissor (m_CurrentViewport.ViewportOffset.x, m_CurrentViewport.ViewportOffset.y, m_CurrentViewport.ViewportSize.x, m_CurrentViewport.ViewportSize.y);
        m_CmdBuffer->SetViewport(m_CurrentViewport.ViewportOffset.x, m_CurrentViewport.ViewportOffset.y, m_CurrentViewport.ViewportSize.x, m_CurrentViewport.ViewportSize.y);
        m_CmdBuffer->SetLineWidth(batch.LineWidth);
        m_CmdBuffer->Bind(m_Pipeline);
        m_CmdBuffer->Bind(m_Set);
        m_CmdBuffer->BeginRenderPass(m_FramebufferPass, m_Framebuffer);
            m_CmdBuffer->BindVertexBuffer(m_VertexBuffer);
            m_CmdBuffer->BindIndexBuffer(m_IndexBuffer, IndicesType::Uint32);
            m_CmdBuffer->DrawIndexed(batch.SubmitedIndicesCount);
        m_CmdBuffer->EndRenderPass();
    }

    m_CmdBuffer->End();

    GPU::Execute(m_CmdBuffer, *wait_semaphore, *signal_semaphore, m_DrawingFence);

    batch.Reset();
    m_BatchRing.Advance();
}

void LineRenderer::Flush() {
    Flush(m_SemaphoreRing.Current(), m_SemaphoreRing.Next());
    m_SemaphoreRing.Advance();
}
