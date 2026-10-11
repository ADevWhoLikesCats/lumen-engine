#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>

// Oriented bounding box: center C, orthonormal axes (X,Y,Z), half-extents H.
struct OBB {
    glm::vec3 C{0};
    glm::vec3 X{1,0,0}, Y{0,1,0}, Z{0,0,1};
    glm::vec3 H{0.5f};

    static OBB fromMatrix(const glm::mat4& m, const glm::vec3& localHalf) {
        OBB o;
        o.C = glm::vec3(m[3]);
        glm::vec3 ax = glm::vec3(m[0]);
        glm::vec3 ay = glm::vec3(m[1]);
        glm::vec3 az = glm::vec3(m[2]);
        o.X = glm::normalize(ax);
        o.Y = glm::normalize(ay);
        o.Z = glm::normalize(az);
        o.H = localHalf * glm::vec3(glm::length(ax), glm::length(ay), glm::length(az));
        return o;
    }

    glm::vec3 axis(int i) const { return i==0 ? X : (i==1 ? Y : Z); }

    void worldAABB(glm::vec3& mn, glm::vec3& mx) const {
        glm::vec3 e = glm::abs(X)*H.x + glm::abs(Y)*H.y + glm::abs(Z)*H.z;
        mn = C - e; mx = C + e;
    }
};

struct Capsule {
    glm::vec3 A{0}, B{0};
    float r = 0.3f;

    glm::vec3 center() const { return 0.5f*(A + B); }
    void translate(const glm::vec3& d) { A += d; B += d; }
};

struct SweepHit {
    bool  hit = false;
    float t   = 1.0f;
    glm::vec3 normal{0};
};
