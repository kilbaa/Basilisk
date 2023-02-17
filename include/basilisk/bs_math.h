#ifndef BS_MATH_H
#define BS_MATH_H

#include <bs_types.h>

#define BS_PI 3.142857
#define BS_SIN_45 0.70710678

double bs_clamp(double d, double min, double max);
int bs_sign(float x);
float bs_fsign(float x);
int bs_closestDivisible(int val, int div);
double bs_fMap(double input, double input_start, double input_end, double output_start, double output_end);
bs_quat bs_eul2quat(bs_vec3 eul);

// void bs_normv3(bs_vec3 *v);
bs_vec2 bs_v2normalize(bs_vec2 v);
bs_vec3 bs_v3normalize(bs_vec3 v);
bs_vec3 bs_cross(bs_vec3 v0, bs_vec3 v1);
float bs_dot(bs_vec3 v0, bs_vec3 v1);
bool bs_triangleIsCCW(bs_vec3 a, bs_vec3 b, bs_vec3 c, bs_vec3 normal);
float bs_signv3(bs_vec3 p1, bs_vec3 p2, bs_vec3 p3);
bool bs_ptInTriangle(bs_vec3 pt, bs_vec3 v1, bs_vec3 v2, bs_vec3 v3);
bs_vec3 bs_triangleNormal(bs_vec3 v0, bs_vec3 v1, bs_vec3 v2);
bs_vec3 bs_triangleCenter(bs_vec3 v0, bs_vec3 v1, bs_vec3 v2);
bs_vec2 bs_v2rot(bs_vec2 pt, bs_vec2 origin, float angle); 
bs_vec3 bs_v3mid(bs_vec3 a, bs_vec3 b);
bs_aabb bs_v3bounds(bs_vec3 *arr, int num_indices);

/* --- QUATERNIONS --- */
void bs_qToMat3(bs_vec4 q, bs_mat3 *out);
bs_quat bs_qMulq(bs_quat q, bs_quat rhs);
bs_quat bs_qNormalize(bs_quat q);
bs_quat bs_qIntegrate(bs_vec4 quat, bs_vec3 dv, float dt);

/* --- BEZIER --- */
double bs_sCubicBez(double p0, double p1, double p2, double p3, double t);
void bs_v2CubicBez(bs_vec2 p0, bs_vec2 p1, bs_vec2 p2, bs_vec2 p3, bs_vec2 *arr, int num_elems);
void bs_v2QuadBez(bs_vec2 p0, bs_vec2 p1, bs_vec2 p2, bs_vec2 *arr, int num_elems);
void bs_cubicBezierPts(bs_vec3 p0, bs_vec3 p1, bs_vec3 p2, bs_vec3 p3, bs_vec3 *arr, int num_elems);

/* --- VECTOR ADDITION --- */
bs_vec2 bs_v2add(bs_vec2 a, bs_vec2 b);
bs_vec3 bs_v3add(bs_vec3 a, bs_vec3 b);
bs_vec3 bs_v3addv2(bs_vec3 a, bs_vec2 b);
bs_vec4 bs_v4add(bs_vec4 a, bs_vec4 b);
bs_vec4 bs_v4addv3(bs_vec4 a, bs_vec3 b);

/* --- VECTOR SUBTRACTION --- */
bs_vec2 bs_v2sub(bs_vec2 a, bs_vec2 b);
bs_vec3 bs_v3sub(bs_vec3 a, bs_vec3 b);
bs_vec3 bs_v3subv2(bs_vec3 a, bs_vec2 b);
bs_vec4 bs_v4sub(bs_vec4 a, bs_vec4 b);
bs_vec4 bs_v4subv3(bs_vec4 a, bs_vec3 b);

/* --- VECTOR MULTIPLICATION --- */
bs_vec2 bs_v2mul(bs_vec2 a, bs_vec2 b);
bs_vec2 bs_v2muls(bs_vec2 a, float s);
bs_vec3 bs_v3mul(bs_vec3 a, bs_vec3 b);
bs_vec3 bs_v3muls(bs_vec3 a, float s);
bs_vec4 bs_v4mul(bs_vec4 a, bs_vec4 b);
bs_vec4 bs_v4muls(bs_vec4 a, float s);

/* --- VECTOR DIVISION --- */
bs_vec2 bs_v2div(bs_vec2 a, bs_vec2 b);
bs_vec2 bs_v2divs(bs_vec2 a, float s);
bs_vec3 bs_v3div(bs_vec3 a, bs_vec3 b);
bs_vec3 bs_v3divs(bs_vec3 a, float s);
bs_vec4 bs_v4div(bs_vec4 a, bs_vec4 b);
bs_vec4 bs_v4divs(bs_vec4 a, float s);

/* --- VECTOR COMPARISON --- */
bool bs_v2cmp(bs_vec2 a, bs_vec2 b);
bool bs_v3cmp(bs_vec3 a, bs_vec3 b);
bool bs_v4cmp(bs_vec4 a, bs_vec4 b);

/* --- RANDOM --- */
int bs_randRangeI(int min, int max);
float bs_randRange(float min, float max);
bs_vec3 bs_randTrianglePt(bs_vec3 p0, bs_vec3 p1, bs_vec3 p2);

#endif /* BS_MATH_H */
