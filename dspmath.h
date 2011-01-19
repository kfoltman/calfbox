// Copyright (C) 2010 Krzysztof Foltman. All rights reserved.

inline float cerp_naive(float v0, float v1, float v2, float v3, float f)
{
    float x0 = -1;
    float x1 = 0;
    float x2 = 1;
    float x3 = 2;
    
    float l0 = ((f - x1) * (f - x2) * (f - x3)) / (            (x0 - x1) * (x0 - x2) * (x0 - x3));
    float l1 = ((f - x0) * (f - x2) * (f - x3)) / ((x1 - x0)             * (x1 - x2) * (x1 - x3));
    float l2 = ((f - x0) * (f - x1) * (f - x3)) / ((x2 - x0) * (x2 - x1)             * (x2 - x3));
    float l3 = ((f - x0) * (f - x1) * (f - x2)) / ((x3 - x0) * (x3 - x1) * (x3 - x2)            );
    
    return v0 * l0 + v1 * l1 + v2 * l2 + v3 * l3;
}

inline float cerp(float v0, float v1, float v2, float v3, float f)
{
    f += 1;
    
    float d0 = (f - 0);
    float d1 = (f - 1);
    float d2 = (f - 2);
    float d3 = (f - 3);
    
    float d03 = (d0 * d3) * (1.0 / 2.0);
    float d12 = (d03 + 1) * (1.0 / 3.0);

    float l0 = -d12 * d3;
    float l1 = d03 * d2;
    float l2 = -d03 * d1;
    float l3 = d12 * d0;
    
    float y = v0 * l0 + v1 * l1 + v2 * l2 + v3 * l3;
    // printf("%f\n", y - cerp_naive(v0, v1, v2, v3, f - 1));
    return y;
}

