#include "disp.h"
#include "bspfile.h"
#include "mathlib.h"
#include "mathtypes.h"
#include "studio_util.h"
#include <vector>
#include <algorithm>

static std::vector<disptriangle_t> g_dispTriangles;
static std::vector<dispbvhnode_t*> g_dispBVHNodes;

static void UpdateDispNodeBounds(dispbvhnode_t* node)
{
    for (int i = 0; i < 3; i++)
    {
        node->mins[i] = MAX_FLOAT_VALUE;
        node->maxs[i] = -MAX_FLOAT_VALUE;
    }

    for (size_t i = 0; i < node->triindexes.size(); i++)
    {
        const disptriangle_t& tri = g_dispTriangles[node->triindexes[i]];
        for (int j = 0; j < 3; j++)
        {
            for (int k = 0; k < 3; k++)
            {
                if (node->mins[k] > tri.v[j][k]) node->mins[k] = tri.v[j][k];
                if (node->maxs[k] < tri.v[j][k]) node->maxs[k] = tri.v[j][k];
            }
        }
    }
}

static void SubdivideDispBVHNode(dispbvhnode_t* node)
{
    if (node->triindexes.size() <= 8)
    {
        node->isleaf = true;
        return;
    }

    vec3_t extents;
    VectorSubtract(node->maxs, node->mins, extents);

    int axis = 0;
    for (int i = 1; i < 3; i++)
    {
        if (extents[i] > extents[axis])
            axis = i;
    }

    float splitPos = node->mins[axis] + extents[axis] * 0.5f;

    std::vector<int> leftTris, rightTris;
    for (size_t i = 0; i < node->triindexes.size(); i++)
    {
        int triIdx = node->triindexes[i];
        if (g_dispTriangles[triIdx].centroid[axis] < splitPos)
            leftTris.push_back(triIdx);
        else
            rightTris.push_back(triIdx);
    }

    if (leftTris.empty() || rightTris.empty())
    {
        node->isleaf = true;
        return;
    }

    dispbvhnode_t* leftChild = new dispbvhnode_t();
    leftChild->triindexes = leftTris;
    node->children[0] = (int)g_dispBVHNodes.size();
    g_dispBVHNodes.push_back(leftChild);
    UpdateDispNodeBounds(leftChild);

    dispbvhnode_t* rightChild = new dispbvhnode_t();
    rightChild->triindexes = rightTris;
    node->children[1] = (int)g_dispBVHNodes.size();
    g_dispBVHNodes.push_back(rightChild);
    UpdateDispNodeBounds(rightChild);

    node->triindexes.clear();
    node->isleaf = false;

    SubdivideDispBVHNode(leftChild);
    SubdivideDispBVHNode(rightChild);
}

void BuildDisplacementBVH()
{
    FreeDisplacementBVH();

    if (g_numdispinfo <= 0)
        return;

    for (int i = 0; i < g_numdispinfo; i++)
    {
        const ddispinfo_t& di = g_ddispinfo[i];
        int N = 1 << di.power;
        int K = N + 1;

        std::vector<vec3_t> gridVerts(K * K);

        for (int y = 0; y < K; y++)
        {
            float v = (float)y / (float)N;
            for (int x = 0; x < K; x++)
            {
                float u = (float)x / (float)N;

                vec3_t basePos;
                for (int c = 0; c < 3; c++)
                {
                    basePos[c] = (1.0f - u) * (1.0f - v) * di.corners[0][c] +
                                 u * (1.0f - v) * di.corners[1][c] +
                                 u * v * di.corners[2][c] +
                                 (1.0f - u) * v * di.corners[3][c];
                }

                int vertIdx = di.vert_start + y * K + x;
                const ddispvert_t& dv = g_ddispverts[vertIdx];

                vec3_t finalPos;
                VectorMA(basePos, dv.distance, dv.vector, finalPos);
                VectorCopy(finalPos, gridVerts[y * K + x]);
            }
        }

        for (int y = 0; y < N; y++)
        {
            for (int x = 0; x < N; x++)
            {
                int idx00 = y * K + x;
                int idx10 = y * K + (x + 1);
                int idx01 = (y + 1) * K + x;
                int idx11 = (y + 1) * K + (x + 1);

                for (int triStep = 0; triStep < 2; triStep++)
                {
                    disptriangle_t tri;
                    if (triStep == 0)
                    {
                        VectorCopy(gridVerts[idx00], tri.v[0]);
                        VectorCopy(gridVerts[idx01], tri.v[1]);
                        VectorCopy(gridVerts[idx11], tri.v[2]);
                    }
                    else
                    {
                        VectorCopy(gridVerts[idx00], tri.v[0]);
                        VectorCopy(gridVerts[idx11], tri.v[1]);
                        VectorCopy(gridVerts[idx10], tri.v[2]);
                    }

                    vec3_t e0, e1;
                    VectorSubtract(tri.v[1], tri.v[0], e0);
                    VectorSubtract(tri.v[2], tri.v[0], e1);
                    CrossProduct(e0, e1, tri.normal);
                    if (!VectorNormalize(tri.normal))
                        continue;

                    VectorAdd(tri.v[0], tri.v[1], tri.centroid);
                    VectorAdd(tri.centroid, tri.v[2], tri.centroid);
                    VectorScale(tri.centroid, 0.333333f, tri.centroid);

                    g_dispTriangles.push_back(tri);
                }
            }
        }
    }

    if (g_dispTriangles.empty())
        return;

    dispbvhnode_t* rootNode = new dispbvhnode_t();
    rootNode->triindexes.resize(g_dispTriangles.size());
    for (size_t i = 0; i < g_dispTriangles.size(); i++)
        rootNode->triindexes[i] = (int)i;

    g_dispBVHNodes.push_back(rootNode);
    UpdateDispNodeBounds(rootNode);
    SubdivideDispBVHNode(rootNode);
}

void FreeDisplacementBVH()
{
    for (size_t i = 0; i < g_dispBVHNodes.size(); i++)
        delete g_dispBVHNodes[i];

    g_dispBVHNodes.clear();
    g_dispTriangles.clear();
}

static bool TestLineTriangle(const vec3_t start, const vec3_t dir, float maxDist, const disptriangle_t& tri)
{
    vec3_t edge1, edge2, h, s, q;
    VectorSubtract(tri.v[1], tri.v[0], edge1);
    VectorSubtract(tri.v[2], tri.v[0], edge2);

    CrossProduct(dir, edge2, h);
    float a = DotProduct(edge1, h);
    if (a > -0.00001f && a < 0.00001f)
        return false;

    float f = 1.0f / a;
    VectorSubtract(start, tri.v[0], s);
    float u = f * DotProduct(s, h);
    if (u < 0.0f || u > 1.0f)
        return false;

    CrossProduct(s, edge1, q);
    float v = f * DotProduct(dir, q);
    if (v < 0.0f || u + v > 1.0f)
        return false;

    float t = f * DotProduct(edge2, q);
    return (t > 0.001f && t < maxDist - 0.001f);
}

static bool IntersectBBoxPoint(const vec_t* start, const vec_t* end, const vec_t* bbmins, const vec_t* bbmaxs, const vec_t* normalDirection)
{
    vec_t tx1 = (bbmins[0] - start[0]) / normalDirection[0];
    vec_t tx2 = (bbmaxs[0] - start[0]) / normalDirection[0];
    vec_t tmin = qmin(tx1, tx2);
    vec_t tmax = qmax(tx1, tx2);

    vec_t ty1 = (bbmins[1] - start[1]) / normalDirection[1];
    vec_t ty2 = (bbmaxs[1] - start[1]) / normalDirection[1];
    tmin = qmax(tmin, qmin(ty1, ty2));
    tmax = qmin(tmax, qmax(ty1, ty2));

    vec_t tz1 = (bbmins[2] - start[2]) / normalDirection[2];
    vec_t tz2 = (bbmaxs[2] - start[2]) / normalDirection[2];
    tmin = qmax(tmin, qmin(tz1, tz2));
    tmax = qmin(tmax, qmax(tz1, tz2));

    return tmax >= tmin && tmin < BOGUS_RANGE && tmax > 0;
}

static bool RecurseTraceDispBVH(const dispbvhnode_t* node, const vec3_t start, const vec3_t end, const vec3_t dir, float len)
{
    if (!IntersectBBoxPoint(start, end, node->mins, node->maxs, dir))
        return false;

    if (node->isleaf)
    {
        for (size_t i = 0; i < node->triindexes.size(); i++)
        {
            if (TestLineTriangle(start, dir, len, g_dispTriangles[node->triindexes[i]]))
                return true;
        }
        return false;
    }

    if (RecurseTraceDispBVH(g_dispBVHNodes[node->children[0]], start, end, dir, len))
        return true;

    return RecurseTraceDispBVH(g_dispBVHNodes[node->children[1]], start, end, dir, len);
}

bool TestLineDisplacement(const vec3_t start, const vec3_t end)
{
    if (g_dispBVHNodes.empty())
        return false;

    vec3_t dir;
    VectorSubtract(end, start, dir);
    float len = VectorNormalize(dir);
    if (len < 0.001f)
        return false;

    return RecurseTraceDispBVH(g_dispBVHNodes[0], start, end, dir, len);
}