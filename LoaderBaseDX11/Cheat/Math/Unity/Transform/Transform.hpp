#pragma once
#include <cstdint>
#include "../../Vectors/Vector4.hpp"
#include "../../Vectors/Vector3.hpp"



extern int ScreenWidth;
extern int ScreenHeight;

struct TMatrix {
    Vector4 Position;
    Vector4 Rotation;
    Vector4 Scale;
};

struct Matrix4x4 {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
        float v[16];
    };
};

class W2S {
public:
    static Vector3 World2Screen(Matrix4x4 ViewMatrix, Vector3 Pos);
    static Vector3 GetCameraWorldPos(uint32_t cameraNativePtr);
};

class Transform {
public:
    static Vector3 get_position_Injected(uint32_t Transform);
    static Vector3 GetHeadPosition(uint32_t Entity);
    static Vector3 GetChestPosition(uint32_t Entity);
    static Vector3 GetRootPosition(uint32_t Entity);
    static Vector3 GetBonePosition(uint32_t Entity, uint32_t boneOffset);
};