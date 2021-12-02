#ifndef STRAITX_2D_CIRCLE_RENDERER_HPP
#define STRAITX_2D_CIRCLE_RENDERER_HPP

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

class CircleRenderer: public NonCopyable{
public:
    struct CircleVertex{
        Vector2f a_Position;
        Vector2f a_Center;
        Vector3f a_Color;
        float    a_Radius;
    };

    static constexpr size_t MaxCirclesInBatch  = 60000;
    static constexpr size_t MaxVerticesInBatch = MaxCirclesInBatch * 4;
    static constexpr size_t MaxIndicesInBatch  = MaxCirclesInBatch * 6;
    static constexpr size_t MaxTexturesInSet   = MaxTexturesBindings;
private:
    struct MatricesUniform{
        Matrix4f u_Projection{1.0f};
    };

    struct Batch{
        Buffer *VerticesBuffer = nullptr;
        Buffer *IndicesBuffer  = nullptr;
        CircleVertex *Vertices = nullptr;
        u32        *Indices  = nullptr;
        size_t      SubmitedCirclesCount = 0;

        Batch();

        ~Batch();

        void Reset();

        bool IsGeometryFull()const{
            return SubmitedCirclesCount == MaxCirclesInBatch;
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

    Ring<Batch, 2> m_BatcheRings;

    SemaphoreRing m_SemaphoreRing;

    MatricesUniform    m_MatricesUniform;
    ViewportParameters m_CurrentViewport;

    Fence m_DrawingFence;

    Buffer *m_VertexBuffer = nullptr;
    Buffer *m_IndexBuffer  = nullptr;
    Buffer *m_MatricesUniformBuffer = nullptr;
public:
    CircleRenderer(const RenderPass *rp);

    ~CircleRenderer();

    Result BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer, const ViewportParameters &viewport);

    Result BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer);

    void EndDrawing(const Semaphore *signal_semaphore);

    void DrawCircle(Vector2s center, float radius, Color color);
private:
    void Flush(const Semaphore *wait_semaphore, const Semaphore *signal_semaphore);
};

#endif//STRAITX_2D_CIRCLE_RENDERER_HPP