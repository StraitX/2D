#ifndef STRAITX_2D_COMMON_VIEWPORT_PARAMETERS_HPP
#define STRAITX_2D_COMMON_VIEWPORT_PARAMETERS_HPP

#include "core/math/vector2.hpp"

struct ViewportParameters{
    Vector2f Scale  = {1.f, 1.f};
    Vector2f Offset = {0.f, 0.f};
    Vector2f ViewportOffset = {0.f, 0.f};
    Vector2f ViewportSize   = {0.f, 0.f};
};

#endif//STRAITX_2D_COMMON_VIEWPORT_PARAMETERS_HPP