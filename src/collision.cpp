// src/collision.cpp
#include "collision.h"

namespace Collision {

glm::vec3 closestPointOnSegment(const glm::vec3& A, const glm::vec3& B, const glm::vec3& P) {
    glm::vec3 ab = B - A;
    float len2 = glm::dot(ab, ab);
    if (len2 < 1e-8f) return A;
    float t = glm::clamp(glm::dot(P - A, ab) / len2, 0.0f, 1.0f);
    return A + t * ab;
}

float pointOBB(const glm::vec3& P, const OBB& b, glm::vec3& outNormal) {
    glm::vec3 d = P - b.C;
    glm::vec3 q( glm::dot(d, b.X), glm::dot(d, b.Y), glm::dot(d, b.Z) );
    glm::vec3 aq = glm::abs(q) - b.H;

    // Outside: closest face determines normal.
    if (aq.x > 0 || aq.y > 0 || aq.z > 0) {
        glm::vec3 c = glm::max(aq, glm::vec3(0));
        float dist = glm::length(c);
        // Build normal from the dominant outside axis.
        glm::vec3 n(0);
        if (aq.x > 0)      n = (q.x > 0 ?  b.X : -b.X);
        else if (aq.y > 0) n = (q.y > 0 ?  b.Y : -b.Y);
        else               n = (q.z > 0 ?  b.Z : -b.Z);
        outNormal = n;
        return dist;
    }
    // Inside: negative penetration on the smallest |aq| axis.
    int axis = 0; float best = aq.x;
    if (aq.y > best) { best = aq.y; axis = 1; }
    if (aq.z > best) { best = aq.z; axis = 2; }
    glm::vec3 n = b.axis(axis);
    if (q[axis] < 0) n = -n;
    outNormal = n;
    return best; // negative
}

float sphereOBB(const glm::vec3& c, float r, const OBB& b, glm::vec3& outNormal) {
    glm::vec3 n;
    float d = pointOBB(c, b, n);          // signed dist from center to box surface
    outNormal = n;
    return r - d;                          // >0 = penetration
}

float capsuleOBB(const Capsule& cap, const OBB& b, glm::vec3& outNormal) {
    // Closest point on the capsule's segment to the OBB center — good
    // approximation because the OBB is small relative to segment length in
    // typical level geometry.
    glm::vec3 p = closestPointOnSegment(cap.A, cap.B, b.C);
    return sphereOBB(p, cap.r, b, outNormal);
}

// ---- Broadphase -------------------------------------------------

static bool segmentAABB(const glm::vec3& a, const glm::vec3& b,
                        const glm::vec3& mn, const glm::vec3& mx,
                        float pad)
{
    glm::vec3 mnp = mn - pad, mxp = mx + pad;
    glm::vec3 d = b - a;
    float tmin = 0.f, tmax = 1.f;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(d[i]) < 1e-8f) {
            if (a[i] < mnp[i] || a[i] > mxp[i]) return false;
        } else {
            float t1 = (mnp[i] - a[i]) / d[i];
            float t2 = (mxp[i] - a[i]) / d[i];
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    return true;
}

// ---- Sphere sweep via conservative advancement -------------------

SweepHit sweepSphereOBB(const glm::vec3& p0, const glm::vec3& p1, float r, const OBB& b) {
    SweepHit out;
    glm::vec3 delta = p1 - p0;
    float dist = glm::length(delta);
    if (dist < 1e-6f) return out;
    glm::vec3 dir = delta / dist;

    // Broadphase — expanded AABB of the OBB by r.
    glm::vec3 mn, mx; b.worldAABB(mn, mx);
    if (!segmentAABB(p0, p1, mn, mx, r)) return out;

    // Conservative advancement. At each step, find closest point on the box
    // to the sphere center; if we're within r, we've hit. Otherwise advance
    // by (gap) along dir.
    float t = 0.f;
    for (int iter = 0; iter < 12; ++iter) {
        glm::vec3 c = p0 + dir * (t * dist);
        glm::vec3 n;
        float d = pointOBB(c, b, n);      // signed distance to *surface*
        float gap = d - r;                // >0 means still separated

        if (gap < 1e-4f) {
            out.hit    = true;
            out.t      = t;
            out.normal = n;
            return out;
        }
        if (t >= 1.f) break;
        // Advance by gap. Small epsilon so we don't stall.
        float step = std::max(gap, 1e-5f) / dist;
        t = std::min(t + step, 1.f);
    }
    return out;
}

SweepHit sweepCapsuleOBB(const Capsule& cap0, const Capsule& cap1, const OBB& b) {
    // Sweep two spheres (one per capsule endpoint) and take the earliest hit.
    // Good enough while the capsule stays roughly upright.
    SweepHit hA = sweepSphereOBB(cap0.A, cap1.A, cap0.r, b);
    SweepHit hB = sweepSphereOBB(cap0.B, cap1.B, cap0.r, b);
    if (hA.hit && (!hB.hit || hA.t <= hB.t)) return hA;
    if (hB.hit) return hB;
    return {};
}

} // namespace Collision
