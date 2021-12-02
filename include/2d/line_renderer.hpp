#ifndef STRAITX_2D_LINE_RENDERER_HPP
#define STRAITX_2D_LINE_RENDERER_HPP

#include "core/math/vector2.hpp"
#include "core/math/vector3.hpp"
#include "core/math/matrix4.hpp"
#include "core/result.hpp"
#include "core/array.hpp"
#include "core/fixed_list.hpp"
#include "core/noncopyable.hpp"
#include "core/raw_var.hpp"
#include "core/ring.hpp"
#include "graphics/color.hpp"
#include "graphics/api/semaphore.hpp"
#include "graphics/api/fence.hpp"
#include "graphics/api/descriptor_set.hpp"

#include "2d/common/semaphore_ring.hpp"
#include "2d/common/viewport_parameters.hpp"

class RenderPass;
class Framebuffer;
class Shader;
class GraphicsPipeline;
class CommandPool;
class CommandBuffer;
class Fence;
class Buffer;
class Texture2D;

class LineRenderer: public NonCopyable{
public:
    struct LineVertex{
        Vector2f a_Position;
        Vector3f a_Color;
    };
    static constexpr size_t MaxVerticesInBatch = 20000 * 4;
    static constexpr size_t MaxIndicesInBatch  = 20000 * 6;
private:
    static constexpr  u32 InvalidLineWidth = -1;

    struct MatricesUniform{
        Matrix4f u_Projection{1.0f};
    };

    struct Batch{
        Buffer *VerticesBuffer = nullptr;
        Buffer *IndicesBuffer  = nullptr;
        LineVertex *Vertices = nullptr;
        u32        *Indices  = nullptr;
        size_t      SubmitedIndicesCount = 0;
        size_t      SubmitedVerticesCount = 0;
        u32         LineWidth = InvalidLineWidth;

        Batch();

        ~Batch();

        void Reset();

        bool IsGeometryFull()const{
            return SubmitedVerticesCount == MaxVerticesInBatch || SubmitedIndicesCount == MaxIndicesInBatch;
        }
    };
private:

    //XXX: do something about allocation
    const RenderPass *m_FramebufferPass = nullptr;
    const Framebuffer *m_Framebuffer = nullptr;
    const DescriptorSetLayout *m_SetLayout = nullptr;
    DescriptorSetPool         *m_SetPool   = nullptr;
    DescriptorSet             *m_Set       = nullptr;

    Array<const Shader *, 2> m_Shaders = {nullptr, nullptr};
    GraphicsPipeline *m_Pipeline       = nullptr;

    CommandPool   *m_CmdPool   = nullptr;
    CommandBuffer *m_CmdBuffer = nullptr;

    Ring<Batch, 2> m_BatchRing;

    SemaphoreRing m_SemaphoreRing;

    MatricesUniform    m_MatricesUniform;
    ViewportParameters m_CurrentViewport;

    Fence m_DrawingFence;

    Buffer *m_VertexBuffer = nullptr;
    Buffer *m_IndexBuffer  = nullptr;
    Buffer *m_MatricesUniformBuffer = nullptr;
public:
    LineRenderer(const RenderPass *rp);

    ~LineRenderer();

    Result BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer, const ViewportParameters &viewport);

    Result BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer);

    void EndDrawing(const Semaphore *signal_semaphore);

    void DrawLines(ConstSpan<Vector2s> points, Color color, u32 width = 1);

    void DrawLine(Vector2s first, Vector2s last, Color color, u32 width = 1){
        Vector2s points[2] = {first, last};
        DrawLines({points, lengthof(points)}, color, width);
    }
private:
    void Flush(const Semaphore *wait_semaphore, const Semaphore *signal_semaphore);
};

#endif//STRAITX_2D_LINE_RENDERER_HPP