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
#include "core/ring.hpp"
#include "graphics/color.hpp"
#include "graphics/api/semaphore.hpp"
#include "graphics/api/descriptor_set.hpp"

class RenderPass;
class Framebuffer;
class Shader;
class GraphicsPipeline;
class CommandPool;
class CommandBuffer;
class Fence;
class Buffer;
class Texture2D;

class RectRenderer: public NonCopyable{
public:
    struct RectVertex{
        Vector2f a_Position;
        Vector2f a_TexCoords;
        Vector3f a_Color;
        float    a_TexIndex;
    };

    static constexpr size_t MaxRectsInBatch    = 60000;
    static constexpr size_t MaxVerticesInBatch = MaxRectsInBatch * 4;
    static constexpr size_t MaxIndicesInBatch  = MaxRectsInBatch * 6;
    static constexpr size_t MaxTexturesInSet   = MaxTexturesBindings;
private:
    struct MatricesUniform{
        Matrix4f u_Projection{1.0f};
    };

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

    struct Batch{
        Buffer *VerticesBuffer = nullptr;
        Buffer *IndicesBuffer  = nullptr;
        RectVertex *Vertices = nullptr;
        u32        *Indices  = nullptr;
        size_t      SubmitedRectsCount = 0;
        FixedList<Texture2D *, MaxTexturesInSet> Textures;

        Batch();

        ~Batch();

        void Reset();

        bool IsGeometryFull()const{
            return SubmitedRectsCount == MaxRectsInBatch;
        }

        bool IsTexturesFull()const{
            return Textures.Size() == Textures.Capacity();
        }

        bool HasTexture(Texture2D *texture){
            return Textures.Find(texture) != Textures.end();
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

    RawVar<SemaphoreRing> m_SemaphoreRing;
    
    MatricesUniform m_MatricesUniform;

    Fence *m_DrawingFence = nullptr;

    Buffer *m_VertexBuffer = nullptr;
    Buffer *m_IndexBuffer  = nullptr;
    Buffer *m_MatricesUniformBuffer = nullptr;

    Texture2D *m_WhiteTexture   = nullptr;
    Sampler   *m_DefaultSampler = nullptr;
    
public:
    Result Initialize(const RenderPass *rp);

    void Finalize();

    bool IsInitialized()const;

    Result BeginDrawing(const Semaphore *wait_semaphore, const Framebuffer *framebuffer);

    void EndDrawing(const Semaphore *signal_semaphore);

    void DrawRect(Vector2s position, Vector2s size, Vector2s origin, float angle, Color color, Texture2D *texture);

    void DrawRect(Vector2s position, Vector2s size, Vector2s origin, float angle, Color color){
        DrawRect(position, size, origin, angle, color, m_WhiteTexture);
    }

    void DrawRect(Vector2s position, Vector2s size, float angle, Color color){
        DrawRect(position, size, size/2, angle, color, m_WhiteTexture);
    }

    void DrawRect(Vector2s position, Vector2s size, Color color, Texture2D *texture){
        DrawRect(position, size, {0, 0}, 0, color, texture);
    }

    void DrawRect(Vector2s position, Vector2s size, Color color){
        DrawRect(position, size, {0, 0}, 0, color, m_WhiteTexture);
    }
private:
    void Flush(const Semaphore *wait_semaphore, const Semaphore *signal_semaphore);

    static void Rotate(Span<Vector2f> vertices, float degrees);
};

#endif//STRAITX-2D_RECT_RENDERER_HPP