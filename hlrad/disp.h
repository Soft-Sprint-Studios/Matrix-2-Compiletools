#ifndef DISP_H
#define DISP_H

#include "qrad.h"
#include <vector>

struct disptriangle_t
{
    vec3_t v[3];
    vec3_t normal;
    vec3_t centroid;
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

#endif // DISP_H