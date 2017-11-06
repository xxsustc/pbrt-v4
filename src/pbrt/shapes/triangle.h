
/*
    pbrt source code is Copyright(c) 1998-2016
                        Matt Pharr, Greg Humphreys, and Wenzel Jakob.

    This file is part of pbrt.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:

    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
    IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
    TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */

#if defined(_MSC_VER)
#define NOMINMAX
#pragma once
#endif

#ifndef PBRT_SHAPES_TRIANGLE_H
#define PBRT_SHAPES_TRIANGLE_H

// shapes/triangle.h*
#include <pbrt/core/shape.h>
#include <pbrt/util/geometry.h>
#include <pbrt/util/stats.h>
#include <pbrt/util/transform.h>
#include <absl/types/span.h>

#include <map>
#include <memory>
#include <vector>

namespace pbrt {

STAT_MEMORY_COUNTER("Memory/Triangle meshes", triMeshBytes);

// Triangle Declarations
struct TriangleMesh {
    // TriangleMesh Public Methods
    TriangleMesh(const Transform &ObjectToWorld, bool reverseOrientation,
                 absl::Span<const int> vertexIndices, absl::Span<const Point3f> p,
                 absl::Span<const Vector3f> S, absl::Span<const Normal3f> N,
                 absl::Span<const Point2f> uv, absl::Span<const int> faceIndices,
                 const std::shared_ptr<const ParamSet> &attributes);

    // TriangleMesh Data
    const bool reverseOrientation, transformSwapsHandedness;
    const int nTriangles, nVertices;
    std::vector<int> vertexIndices;
    std::vector<Point3f> p;
    std::vector<Normal3f> n;
    std::vector<Vector3f> s;
    std::vector<Point2f> uv;
    std::vector<int> faceIndices;
    std::shared_ptr<const ParamSet> attributes;
};

class Triangle : public Shape {
  public:
    // Triangle Public Methods
    Triangle(const std::shared_ptr<TriangleMesh> &mesh, int triIndex)
        : mesh(mesh), triIndex(triIndex) {
        triMeshBytes += sizeof(*this);
    }
    Bounds3f WorldBound() const;
    bool Intersect(const Ray &ray, Float *tHit, SurfaceInteraction *isect) const;
    bool IntersectP(const Ray &ray) const;
    Float Area() const;

    Interaction Sample(const Interaction &ref, const Point2f &u,
                       Float *pdf) const;
    Float Pdf(const Interaction &ref, const Vector3f &wi) const;
    Interaction Sample(const Point2f &u, Float *pdf) const;

    // Returns the solid angle subtended by the triangle w.r.t. the given
    // reference point p.
    Float SolidAngle(const Point3f &p, int nSamples = 0) const;

    bool ReverseOrientation() const { return mesh->reverseOrientation; }
    bool TransformSwapsHandedness() const {
        return mesh->transformSwapsHandedness;
    }
    const ParamSet *GetAttributes() const { return mesh->attributes.get(); }

  private:
    // Triangle Private Methods
    std::array<Point2f, 3> GetUVs() const {
        if (mesh->uv.size() > 0) {
            const int *v = &mesh->vertexIndices[3 * triIndex];
            return { mesh->uv[v[0]], mesh->uv[v[1]], mesh->uv[v[2]] };
        } else
            return { Point2f(0, 0), Point2f(1, 0), Point2f(1, 1) };
    }

    // Triangle Private Data
    std::shared_ptr<TriangleMesh> mesh;
    int triIndex;
};

std::vector<std::shared_ptr<Shape>> CreateTriangleMesh(
    const Transform &ObjectToWorld, const Transform &WorldToObject,
    bool reverseOrientation, absl::Span<const int> vertexIndices,
    absl::Span<const Point3f> p, absl::Span<const Vector3f> s,
    absl::Span<const Normal3f> n, absl::Span<const Point2f> uv,
    absl::Span<const int> faceIndices,
    const std::shared_ptr<const ParamSet> &attributes);


std::vector<std::shared_ptr<Shape>> CreateTriangleMeshShape(
    std::shared_ptr<const Transform> ObjectToWorld,
    std::shared_ptr<const Transform> WorldToObject, bool reverseOrientation,
    const ParamSet &params, const std::shared_ptr<const ParamSet> &attributes);

bool WritePlyFile(const std::string &filename,
                  absl::Span<const int> vertexIndices,
                  absl::Span<const Point3f> P, absl::Span<const Vector3f> S,
                  absl::Span<const Normal3f> N, absl::Span<const Point2f> UV);

}  // namespace pbrt

#endif  // PBRT_SHAPES_TRIANGLE_H