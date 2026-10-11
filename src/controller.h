#pragma once
#include "collider.h"
#include "collision.h"
#include <vector>
#include <algorithm>

struct CharacterController {
    glm::vec3 pos{0, 1.0f, 5};
    float height     = 1.7f;
    float radius     = 0.3f;
    float eyeOffset  = 0.65f;
    bool  grounded   = false;

    Capsule capsule() const {
        Capsule c;
        float halfSeg = std::max(0.0f, (height - 2.f*radius)) * 0.5f;
        c.A = pos - glm::vec3(0, halfSeg, 0);
        c.B = pos + glm::vec3(0, halfSeg, 0);
        c.r = radius;
        return c;
    }

    glm::vec3 eye() const { return pos + glm::vec3(0, eyeOffset, 0); }

    void move(const glm::vec3& desired, const std::vector<OBB>& world) {
        grounded = false;
        glm::vec3 remaining = desired;

        // Up to 4 planes so we can resolve a 3-face corner cleanly.
        glm::vec3 planes[4];
        int nPlanes = 0;

        for (int iter = 0; iter < 4; ++iter) {
            if (glm::dot(remaining, remaining) < 1e-8f) break;

            Capsule c0 = capsule();
            Capsule c1 = c0;
            c1.translate(remaining);

            float bestT = 1.f;
            glm::vec3 bestN(0);
            bool any = false;
            for (auto& b : world) {
                SweepHit h = Collision::sweepCapsuleOBB(c0, c1, b);
                if (h.hit && h.t < bestT) {
                    bestT = h.t; bestN = h.normal; any = true;
                }
            }

            // Advance close to contact, then push out along N by a skin
            // so the next sweep starts with real clearance.
            float safeT = std::max(0.f, bestT - 1e-3f);
            pos += remaining * safeT;

            if (!any) break;

            // Skin push-out. This is what unsticks you from corners.
            pos += bestN * 1e-3f;

            if (bestN.y > 0.7f) grounded = true;

            // Remember this plane (dedupe near-parallel ones).
            bool dup = false;
            for (int i = 0; i < nPlanes; ++i) {
                if (glm::dot(planes[i], bestN) > 0.99f) { dup = true; break; }
            }
            if (!dup && nPlanes < 4) planes[nPlanes++] = bestN;

            // Advance remaining past the contact, then project it out of
            // every plane we've touched this frame.
            remaining = remaining * (1.f - bestT);
            for (int i = 0; i < nPlanes; ++i) {
                float d = glm::dot(remaining, planes[i]);
                if (d < 0.f) remaining -= planes[i] * d;
            }
        }
    }
};
