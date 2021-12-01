#include "2d/common/semaphore_ring.hpp"
#include "core/assert.hpp"

SemaphoreRing::SemaphoreRing(){
    FullRing[1] = &LoopingPart[0];
    FullRing[2] = &LoopingPart[1];
}

void SemaphoreRing::Begin(const Semaphore *first){
    SX_CORE_ASSERT(FullRing[0] == nullptr, "RectRenderer: SemaphoreRing should be ended");
    Index = 0;
    FullRing[0] = first;
}

void SemaphoreRing::End(){
    FullRing[0] = nullptr;
}

const Semaphore *SemaphoreRing::Current(){
    return FullRing[Index];
}

const Semaphore *SemaphoreRing::Next(){
    return FullRing[NextIndex()];
}

void SemaphoreRing::Advance(){
    Index = NextIndex();
}

u32 SemaphoreRing::NextIndex(){
    u32 next_index = Index + 1;
    if(next_index == 3)
        next_index = 1;
    return next_index;
}