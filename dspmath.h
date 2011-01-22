/*
Calf Box, an open source musical instrument.
Copyright (C) 2010 Krzysztof Foltman

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

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

