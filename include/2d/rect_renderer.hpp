#ifndef STRAITX_2D_RECT_RENDERER_HPP
#define STRAITX_2D_RECT_RENDERER_HPP

#include "core/math/vector2.hpp"
#include "core/math/vector3.hpp"
#include "core/math/matrix4.hpp"
#include "core/unique_ptr.hpp"
#include "core/array.hpp"
#include "core/fixed_list.hpp"
#include "core/noncopyable.hpp"
#include "graphics/color.hpp"
#include "graphics/api/semaphore.hpp"
#include "graphics/api/fence.hpp"
#include "graphics/api/descriptor_set.hpp"
#include "graphics/api/framebuffer.hpp"
#include "graphics/api/graphics_pipeline.hpp"
#include "2d/common/viewport_parameters.hpp"

class RenderPass;
class Shader;
class GraphicsPipeline;
class CommandPool;
class CommandBuffer;
class Fence;
class Buffer;
class Texture2D;

class RectRenderer: public NonCopyable{
    static const Array<Vector2f, 4> s_DefaultTextureCoordinates;

    struct RectVertex{
        Vector2f a_Position;
        Vector2f a_TexCoords;
        u32      a_Color;
        float    a_TexIndex;
    };

    struct MatricesUniform{
        Matrix4f u_Projection{1.0f};
    };


    struct Batch {
        static constexpr size_t MaxTexturesInBatch = 15;

        FixedList<const Texture2D*, MaxTexturesInBatch> Textures;
        UniquePtr<RectVertex[]> Vertices;
        UniquePtr<u16[]> Indices;
        UniquePtr<Buffer> VertexBuffer;
        UniquePtr<Buffer> IndexBuffer;
        size_t SubmitedPrimitives = 0;

        Batch(u16 max_primitives_count);

        Batch(Batch&&)noexcept = default;
        
        bool IsFull()const;

        void PushRect(Vector2f position, Vector2f size, Vector2f origin, float angle, Color color, Texture2D* texture, const Array<Vector2f, 4>& texture_coords = s_DefaultTextureCoordinates);

        u16 MaxPrimitivesCount()const;

        void Reset() {
            SubmitedPrimitives = 0;
            Textures.Clear();
        }
    };

private:

    const RenderPass *m_FramebufferPass = nullptr;
    UniquePtr<DescriptorSetLayout> m_SetLayout;

    static constexpr size_t MaxSets = 16;
    static constexpr size_t PreallocatedSets = 1;
    SingleFrameDescriptorSetPool m_SetPool{ {MaxSets, m_SetLayout.Get()}, PreallocatedSets };

    UniquePtr<GraphicsPipeline> m_Pipeline;
    StructBuffer<MatricesUniform> m_MatricesUniformBuffer;

    UniquePtr<Sampler> m_DefaultSampler{
        Sampler::Create({})
    };

    List<Batch> m_Batches;
public:
    RectRenderer(const RenderPass *rp);

    void DrawRect(Vector2f position, Vector2f size, Vector2f origin, float angle, Color color, Texture2D *texture, const Array<Vector2f, 4> &texture_coords = s_DefaultTextureCoordinates);

    void DrawRect(Vector2f position, Vector2f size, Vector2f origin, float angle, Color color){
        DrawRect(position, size, origin, angle, color, Texture2D::White());
    }

    void DrawRect(Vector2f position, Vector2f size, float angle, Color color){
        DrawRect(position, size, size/2.f, angle, color, Texture2D::White());
    }

    void DrawRect(Vector2f position, Vector2f size, Color color, Texture2D *texture){
        DrawRect(position, size, {0.f, 0.f}, 0, color, texture);
    }

    void DrawRect(Vector2f position, Vector2f size, Color color){
        DrawRect(position, size, {0.f, 0.f}, 0, color, Texture2D::White());
    }

    void CmdRender(CommandBuffer *cmd_buffer, const Framebuffer *fb, const ViewportParameters &viewport);

    void CmdRender(CommandBuffer* cmd_buffer, const Framebuffer* fb) {
        ViewportParameters default_parameters;
        default_parameters.ViewportOffset = {0.f, 0.f};
        default_parameters.ViewportSize = Vector2f(fb->Size());
        CmdRender(cmd_buffer, fb, default_parameters);
    }
};

#endif//STRAITX-2D_RECT_RENDERER_HPP