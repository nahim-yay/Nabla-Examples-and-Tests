// Copyright (C) 2018-2020 - DevSH Graphics Programming Sp. z O.O.
// This file is part of the "Nabla Engine".
// For conditions of distribution and use, see copyright notice in nabla.h

#define _NBL_STATIC_LIB_
#include <iostream>
#include <cstdio>
#include <assert.h>
#include <nabla.h>
#include <nbl/builtin/hlsl/cpp_compat/matrix.h>
#include <nbl/builtin/hlsl/cpp_compat/vector.h>


using namespace nbl;
using namespace core;
using namespace ui;
using namespace hlsl;
/*
    Uncomment for more detailed logging
*/

// #define NBL_MORE_LOGS


struct S {
    float3 f;
};

struct T {
    float    a;
    float3   b;
    S        c;
    float2x3 d;
    float2x3 e;
    int      f[3];
    float2   g[2];
    float4   h;
};

int main()
{
    {
        float4x3 a;
        float3x4 b;
        float3 v;
        float4 u;

        static_assert(std::is_same_v<float4x4, decltype(a * b)>);
        static_assert(std::is_same_v<float3x3, decltype(b * a)>);
        static_assert(std::is_same_v<float4, decltype(a * v)>);
        static_assert(std::is_same_v<float4, decltype(v * b)>);
        static_assert(std::is_same_v<float3, decltype(u * a)>);
        static_assert(std::is_same_v<float3, decltype(b * u)>);
    }

    static_assert(offsetof(T, a) == 0);
    static_assert(offsetof(T, b) == offsetof(T, a) + sizeof(T::a));
    static_assert(offsetof(T, c) == offsetof(T, b) + sizeof(T::b));
    static_assert(offsetof(T, d) == offsetof(T, c) + sizeof(T::c));
    static_assert(offsetof(T, e) == offsetof(T, d) + sizeof(T::d));
    static_assert(offsetof(T, f) == offsetof(T, e) + sizeof(T::e));
    static_assert(offsetof(T, g) == offsetof(T, f) + sizeof(T::f));
    static_assert(offsetof(T, h) == offsetof(T, g) + sizeof(T::g));
    
}