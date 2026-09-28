#include "Math.hpp"

namespace Math {
    const float PI = 3.14159265358979323846f;
    const float DEG2RAD = PI / 180.0f;
    const float RAD2DEG = 180.0f / PI;
    const float EPSILON = 1e-5f;

    float abs(float value) {
        return value < 0 ? -value : value;
    }

    float sqrt(float value) {
        if (value <= 0) return 0;

        float x = value;
        float y = 1.0f;

        for (int i = 0; i < 10; ++i) {
            x = (x + y) * 0.5f;
            y = value / x;
        }
        return x;
    }

    float sin(float x) {
        while (x > PI) x -= 2 * PI;
        while (x < -PI) x += 2 * PI;

        float x2 = x * x;
        float x3 = x * x2;
        float x5 = x3 * x2;
        float x7 = x5 * x2;

        return x - x3 / 6.0f + x5 / 120.0f - x7 / 5040.0f;
    }

    float cos(float x) {
        return sin(PI / 2 - x);
    }

    float atan2(float y, float x) {
        if (x == 0) {
            if (y > 0) return PI / 2;
            if (y < 0) return -PI / 2;
            return 0;
        }

        float atan = y / x;
        if (x < 0) {
            if (y >= 0) return atan + PI;
            return atan - PI;
        }
        return atan;
    }

    float acos(float x) {
        if (x >= 1.0f) return 0;
        if (x <= -1.0f) return PI;

        return PI / 2 - x - (x * x * x) / 6.0f - (3.0f * x * x * x * x * x) / 40.0f;
    }

    float max(float a, float b) {
        return a > b ? a : b;
    }

    float min(float a, float b) {
        return a < b ? a : b;
    }

    bool approximately(float a, float b) {
        return abs(a - b) < EPSILON;
    }
}