#pragma once

namespace Math {
    extern const float PI;
    extern const float DEG2RAD;
    extern const float RAD2DEG;
    extern const float EPSILON;

    float abs(float value);
    float sqrt(float value);
    float sin(float x);
    float cos(float x);
    float atan2(float y, float x);
    float acos(float x);
    float (max)(float a, float b);
    float (min)(float a, float b);
    bool approximately(float a, float b);
}