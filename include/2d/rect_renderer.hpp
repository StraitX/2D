#ifndef STRAITX_2D_RECT_RENDERER_HPP
#define STRAITX_2D_RECT_RENDERER_HPP

#include "core/math/vector2.hpp"
#include "core/math/vector3.hpp"
#include "core/math/matrix4.hpp"
#include "core/result.hpp"
#include "core/array.hpp"
#include "core/fixed_list.hpp"
#include "core/noncopyable.hpp"
#include "core/raw_var.hpp"
#include "graphics/color.hpp"
#include "graphics/api/semaphore.hpp"

class DescriptorSetLayout;
class DescriptorSet;
class DescriptorSetPool;
class RenderPass;
class Framebuffer;
class Shader;
class GraphicsPipeline;
class CommandPool;
class CommandBuffer;
class Fence;
class Buffer;

class RectRenderer: public NonCopyable{
public:
    struct RectVertex{
        Vector2f a_Position;
        Vector2f a_TexCoords;
        Vector3f a_Color;
        float    a_TexIndex;
    };

    static constexpr size_t MaxRectsInBatch    = 20000;
    static constexpr size_t MaxVerticesInBatch = MaxRectsInBatch * 4;
    static constexpr size_t MaxIndicesInBatch  = MaxRectsInBatch * 6;
private:
    struct MatricesUniform{
        Matrix4f u_Projection{1.0f};
    };
private:

    //XXX: do something about allocation
    const RenderPass *m_FramebufferPass = nullptr;
    const DescriptorSetLayout *m_SetLayout = nullptr;
    DescriptorSetPool         *m_SetPool   = nullptr;
    DescriptorSet             *m_Set       = nullptr;

    Array<const Shader *, 2> m_Shaders = {nullptr, nullptr};
    GraphicsPipeline *m_Pipeline       = nullptr;

    CommandPool   *m_CmdPool   = nullptr;
    CommandBuffer *m_CmdBuffer = nullptr;

    struct BatchData{
        RectVertex *Vertices = nullptr;
        u32        *Indices  = nullptr;    
        size_t      SubmitedRectsCount = 0;   

        const Framebuffer *TargetFramebuffer = nullptr; 

        void Begin(const Framebuffer *fb);

        void End();
    }m_CurrentBatch;

    class SemaphoreRing{
    private:
        Semaphore LoopingPart[2] = {};
        const Semaphore *FullRing[3] = {nullptr, nullptr, nullptr};
        u32 Index = 0;
    public:
        SemaphoreRing();

        void Begin(const Semaphore *first);

        void End();

        const Semaphore *Current();
        const Semaphore *Next();

        void Advance();

        u32 NextIndex();
    };

    RawVar<SemaphoreRing> m_SemaphoreRing;
    
    MatricesUniform m_MatricesUniform;

    Fence *m_DrawingFence = nullptr;

    Buffer *m_VertexBuffer = nullptr;
    Buffer *m_IndexBuffer  = nullptr;
    Buffer *m_MatricesUniformBuffer = nullptr;
    
public:
    Result Initialize(const RenderPass *rp);

    void Finalize();

    bool IsInitialized()const;

    Result BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer);

    void EndDrawing(const Semaphore *signal_semaphore);

    void DrawRect(Vector2s position, Vector2s size, Color color);
private:
    void Flush(const Semaphore *wait_semaphore, const Semaphore *signal_semaphore);
};

#endif//STRAITX-2D_RECT_RENDERER_HPP