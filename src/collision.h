// src/collision.h
#pragma once
#include "collider.h"

namespace Collision {

// Closest point on segment [A,B] to point P.
glm::vec3 closestPointOnSegment(const glm::vec3& A, const glm::vec3& B, const glm::vec3& P);

// Signed distance from a point to an OBB surface (positive outside).
// Also writes the outward normal at the closest face.
float pointOBB(const glm::vec3& P, const OBB& b, glm::vec3& outNormal);

// Sphere-vs-OBB: returns penetration depth (<=0 means separated).
float sphereOBB(const glm::vec3& c, float r, const OBB& b, glm::vec3& outNormal);

// Capsule-vs-OBB penetration (uses sphere-OBB on the closest point).
float capsuleOBB(const Capsule& cap, const OBB& b, glm::vec3& outNormal);

// Sweep a sphere of radius r from p0 along (p1-p0) against an OBB.
// Returns first hit t in [0,1] if any. Conservative advancement, 4-8 iters.
SweepHit sweepSphereOBB(const glm::vec3& p0, const glm::vec3& p1, float r, const OBB& b);

// Sweep a capsule (kept vertical) against an OBB.
SweepHit sweepCapsuleOBB(const Capsule& cap0, const Capsule& cap1, const OBB& b);

} // namespace Collision
