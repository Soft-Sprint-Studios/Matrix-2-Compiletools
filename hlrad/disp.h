/*
 * MIT License
 *
 * Copyright (c) 2025-2026 Soft Sprint Studios
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef DISP_H
#define DISP_H

#include "qrad.h"
#include <vector>

struct disptriangle_t
{
    vec3_t v[3];
    vec3_t normal;
    vec3_t centroid;
    int facenum;
};

struct dispbvhnode_t
{
    vec3_t mins;
    vec3_t maxs;
    int children[2];
    bool isleaf;
    std::vector<int> triindexes;
};

void BuildDisplacementBVH();
void FreeDisplacementBVH();
bool TestLineDisplacement(const vec3_t start, const vec3_t end);
bool GetDisplacementSample(int facenum, const vec3_t base_spot, vec3_t out_spot, vec3_t out_normal);

#endif // DISP_H