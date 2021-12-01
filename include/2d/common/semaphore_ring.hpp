#ifndef STRAITX_2D_COMMON_SEMAPHORE_RING_HPP
#define STRAITX_2D_COMMON_SEMAPHORE_RING_HPP

#include "core/noncopyable.hpp"
#include "graphics/api/semaphore.hpp"

class SemaphoreRing: public NonCopyable{
private:
    Semaphore LoopingPart[2] = {};
    const Semaphore *FullRing[3] = {nullptr, nullptr, nullptr};
    u32 Index = 0;
public:
    SemaphoreRing();

    ~SemaphoreRing() = default;

    void Begin(const Semaphore *first);

    void End();

    const Semaphore *Current();
    const Semaphore *Next();

    void Advance();

    u32 NextIndex();
};

#endif//STRAITX_2D_COMMON_SEMAPHORE_RING_HPP