#ifndef UVSR_PROCEDURAL_SKY_SHARED_H
#define UVSR_PROCEDURAL_SKY_SHARED_H

// These integer constants are shared by the C++ reference test and HLSL star
// generator. Integer hashing keeps the star field deterministic across frames;
// no screen coordinate, frame index, or temporal jitter enters the identity of
// a star.
#define UVSR_SKY_STAR_SEED 0x9e3779b9u
#define UVSR_SKY_STAR_SEED_OFFSET_X 0x68bc21ebu
#define UVSR_SKY_STAR_SEED_OFFSET_Y 0x02e5be93u
#define UVSR_SKY_STAR_SEED_SIZE 0xa511e9b3u
#define UVSR_SKY_STAR_SEED_COLOR 0x63d83595u
#define UVSR_SKY_STAR_SEED_CLUSTER 0xb5297a4du
#define UVSR_SKY_STAR_SEED_CLUSTER_DETAIL 0x1b56c4e9u
#define UVSR_SKY_STAR_HASH_MASK 0x00ffffffu
#define UVSR_SKY_STAR_HASH_SCALE (1.0 / 16777216.0)
#define UVSR_SKY_STAR_RADIUS_MIN 0.025
#define UVSR_SKY_STAR_RADIUS_COMMON_MAX 0.11
#define UVSR_SKY_STAR_RADIUS_MAX 0.16
#define UVSR_SKY_STAR_LARGE_START 0.88
#define UVSR_SKY_STAR_CLUSTER_CELL_SCALE 24.0
#define UVSR_SKY_STAR_CLUSTER_DETAIL_SCALE 8.0
#define UVSR_SKY_STAR_CLUSTER_LOW 0.28
#define UVSR_SKY_STAR_CLUSTER_HIGH 0.74
#define UVSR_SKY_STAR_CLUSTER_DENSITY_MIN 0.12
#define UVSR_SKY_STAR_CLUSTER_DENSITY_MAX 2.40

#endif // UVSR_PROCEDURAL_SKY_SHARED_H
