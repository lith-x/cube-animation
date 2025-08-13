#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define ARENA_IMPLEMENTATION
#include "arena.h"
#include "raylib.h"

#define MIN_SPEED 100.0f
#define MAX_SPEED 150.0f
#define XOR_MAGIC 0x2545F4914F6CDD1D
#define DEFAULT_SEED 12345
#define X_MIN -100.0f
#define X_MAX 100.0f
#define Y_MIN -100.0f
#define Y_MAX 100.0f
#define Z_MIN -100.0f
#define Z_MAX 100.0f
#define CUBE_SPACING 4.0f
#define BULLET_POOL_SIZE 20
#define MIN_SPAWN_DELAY 0.1f
#define MAX_SPAWN_DELAY 0.5f
#define EPSILON 0.1f
#define MAX_CUBE_SIZE 2.0f
#define MAX_BULLET_LENGTH 7.0f
#define MIN_BULLET_LENGTH 3.0f
#define MAX_BULLET_WIDTH 3.0f
#define MIN_BULLET_WIDTH 1.0f
#define GRID_X ((X_MAX - X_MIN) / CUBE_SPACING)
#define GRID_Y ((Y_MAX - Y_MIN) / CUBE_SPACING)
#define GRID_Z ((Z_MAX - Z_MIN) / CUBE_SPACING)
#define CUBE_COUNT (int)(GRID_X * GRID_Y * GRID_Z)

// todo: feed some unique value to this at runtime
static uint32_t xorshift_state = DEFAULT_SEED;

typedef struct Points {
    Vector3 positions[BULLET_POOL_SIZE];               // CPU/GPU
    Vector3 scales[BULLET_POOL_SIZE];                  // GPU
    Color colors[BULLET_POOL_SIZE];                    // GPU
    enum Direction {
        X_POSITIVE = (uint8_t)0x01,
        X_NEGATIVE = (uint8_t)0x02,
        Y_POSITIVE = (uint8_t)0x04,
        Y_NEGATIVE = (uint8_t)0x08,
        Z_POSITIVE = (uint8_t)0x10,
        Z_NEGATIVE = (uint8_t)0x20,
        UNSPAWNED  = (uint8_t)0x00,
        DIRECTION_LEN = 6
    } directions[BULLET_POOL_SIZE];                    // CPU
    float speeds[BULLET_POOL_SIZE];                    // CPU
    int next_free[BULLET_POOL_SIZE];
} Points;

typedef struct BulletFreeList {
    int head_idx;
    int tail_idx;
} BulletFreeList;

static inline float dir_to_start_pos(Points *p, int idx) {
    switch (p->directions[idx]) {
    case X_POSITIVE:
        return X_MIN;
    case X_NEGATIVE:
        return X_MAX;
    case Y_POSITIVE:
        return Y_MIN;
    case Y_NEGATIVE:
        return Y_MAX;
    case Z_POSITIVE:
        return Z_MIN;
    case Z_NEGATIVE:
        return Z_MAX;
    default:
        return 0;
    }
}

// xorshift* https://rosettacode.org/wiki/Pseudo-random_numbers/Xorshift_star#C
uint32_t next_rand() {
    uint64_t x = xorshift_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    xorshift_state = x;
    return (x * XOR_MAGIC) >> 32;
}

static inline float next_randf(float min, float max) {
    return min + (max - min) * (float)next_rand() / (float)UINT32_MAX;
}

static inline float *get_movement_axis(Vector3 *v, int dir) {
    float *ptr = (float *)v;
    switch (dir) {
    case Z_POSITIVE:
    case Z_NEGATIVE:
        ptr++;
        __attribute__((fallthrough));
    case Y_POSITIVE:
    case Y_NEGATIVE:
        ptr++;
    default:
    }
    return ptr;
}

// NOTE: avoid duplication by checking is_spawned on point before calling
void free_point(BulletFreeList *frie, Points *points, int idx) {
    // todo: see if this is necessary? fixed some flickering before, but wondering
    points->positions[idx] = (Vector3){FLT_MAX, FLT_MAX, FLT_MAX};
    points->directions[idx] = UNSPAWNED;
    points->next_free[frie->tail_idx] = idx;
    frie->tail_idx = idx;
}

void spawn_point_if_available(BulletFreeList *frie, Points *points) {
    if (points->directions[frie->head_idx] != UNSPAWNED) return;
    int idx = frie->head_idx;
    frie->head_idx = points->next_free[frie->head_idx];

    points->colors[idx] =
        ColorLerp((Color){0xC7, 0x51, 0x08, 0xFF},
                  (Color){0x61, 0x0C, 0xCF, 0xFF}, next_randf(0.0f, 1.0f));
    points->speeds[idx] = next_randf(MIN_SPEED, MAX_SPEED);

    points->directions[idx] = 1 << (next_rand() % DIRECTION_LEN);

    float bullet_width = next_randf(MIN_BULLET_WIDTH, MAX_BULLET_WIDTH);
    points->scales[idx] = (Vector3){bullet_width, bullet_width, bullet_width};
    float *scale_move_axis = get_movement_axis(&points->scales[idx], points->directions[idx]);
    *scale_move_axis = next_randf(MIN_BULLET_LENGTH, MAX_BULLET_LENGTH);

    float px =
        floorf(CUBE_SPACING / 2.0f + next_randf(X_MIN, X_MAX) / CUBE_SPACING) *
        CUBE_SPACING;
    float py =
        floorf(CUBE_SPACING / 2.0f + next_randf(Y_MIN, Y_MAX) / CUBE_SPACING) *
        CUBE_SPACING;
    float pz =
        floorf(CUBE_SPACING / 2.0f + next_randf(Z_MIN, Z_MAX) / CUBE_SPACING) *
        CUBE_SPACING;
    points->positions[idx] = (Vector3){px, py, pz};
    float *pos_move_axis = get_movement_axis(&points->positions[idx], points->directions[idx]);
    *pos_move_axis =
        dir_to_start_pos(points, idx) +
        ((points->directions[idx] & (X_NEGATIVE | Y_NEGATIVE | Z_NEGATIVE)) ? 1.0f
                                                                 : -1.0f) *
            MAX_BULLET_LENGTH * (*scale_move_axis);
}

static inline Matrix ToTranslationMatrix(Vector3 v) {
    return (Matrix){1.0f, 0.0f, 0.0f, v.x, 0.0f, 1.0f, 0.0f, v.y,
                    0.0f, 0.0f, 1.0f, v.z, 0.0f, 0.0f, 0.0f, 1.0f};
}

int main() {
    Points points = {0};

    BulletFreeList frie = {0};

    for (int i = 0; i < BULLET_POOL_SIZE; i++) {
        free_point(&frie, &points, i);
    }

    Vector3 cubePositions[CUBE_COUNT];
    int cube_idx = 0;
    for (float z = Z_MIN; z < Z_MAX; z += CUBE_SPACING) {
        for (float y = Y_MIN; y < Y_MAX; y += CUBE_SPACING) {
            for (float x = X_MIN; x < X_MAX; x += CUBE_SPACING) {
                cubePositions[cube_idx++] = (Vector3){x, y, z};
            }
        }
    }

    Camera3D camera = {.position = (Vector3){X_MIN * 5.5f, 0.0f, 30.0f},
                       .target = (Vector3){(X_MAX + X_MIN) / 2.0f,
                                           (Y_MAX + Y_MIN) / 2.0f,
                                           (Z_MAX + Z_MIN) / 2.0f},
                       .up = (Vector3){0.0f, 0.0f, 1.0f},
                       .fovy = 30.0f,
                       .projection = CAMERA_PERSPECTIVE};

    InitWindow(800, 600, "hi");
    float spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);
    spawn_point_if_available(&frie, &points);
    float dt;
    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));
    char count_text[20];

    // Mesh cube = GenMeshCube(20.0f, 20.0f, 20.0f);
    // Material mat = LoadMaterialDefault();
    // mat.shader = LoadShader("cubegrid.vs", "cubegrid.fs");

    // Matrix transforms[CUBE_COUNT];
    // for (int i = 0; i < CUBE_COUNT; i++) {
    //     transforms[i] = ToTranslationMatrix(cubePositions[i]);
    // }

    while (!WindowShouldClose()) {
        int point_count = 0;
        dt = GetFrameTime();
        spawn_timer -= dt;
        if (spawn_timer <= 0.0f) {
            spawn_point_if_available(&frie, &points);
            spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);
        }
        BeginDrawing();
        // UpdateCamera(&camera, CAMERA_ORBITAL);
        ClearBackground(BLACK);

        BeginMode3D(camera);
        for (int i = 0; i < BULLET_POOL_SIZE; i++) {
            if (points.positions[i].x == FLT_MAX)
                continue;
            point_count++;

            // update point position
            float *override = get_movement_axis(&points.positions[i], points.directions[i]);
            float negative =
                points.directions[i] & (X_NEGATIVE | Y_NEGATIVE | Z_NEGATIVE) ? -1.0f
                                                                      : 1.0f;
            *override += negative * points.speeds[i] * dt;

            // bounds check
            if (points.positions[i].x < X_MIN - MAX_BULLET_LENGTH * points.scales[i].x ||
                points.positions[i].x > X_MAX + MAX_BULLET_LENGTH * points.scales[i].x ||
                points.positions[i].y < Y_MIN - MAX_BULLET_LENGTH * points.scales[i].y ||
                points.positions[i].y > Y_MAX + MAX_BULLET_LENGTH * points.scales[i].y ||
                points.positions[i].z < Z_MIN - MAX_BULLET_LENGTH * points.scales[i].z ||
                points.positions[i].z > Z_MAX + MAX_BULLET_LENGTH * points.scales[i].z)
                free_point(&frie, &points, i);

            for (int z = Z_MIN; z < Z_MAX; z += CUBE_SPACING) {
                for (int y = Y_MIN; y < Y_MAX; y += CUBE_SPACING) {
                    for (int x = X_MIN; x < X_MAX; x += CUBE_SPACING) {
                        float dx =
                            fabsf(((float)x - points.positions[i].x) / points.scales[i].x);
                        float dy =
                            fabsf(((float)y - points.positions[i].y) / points.scales[i].y);
                        float dz =
                            fabsf(((float)z - points.positions[i].z) / points.scales[i].z);
                        float dist = dx + dy + dz;
                        if (dist >= MAX_BULLET_LENGTH)
                            continue;
                        float side_len =
                            MAX_CUBE_SIZE * (1.0f - dist / MAX_BULLET_LENGTH);
                        if (side_len < EPSILON)
                            continue;
                        else if (side_len > MAX_CUBE_SIZE)
                            side_len = MAX_CUBE_SIZE;
                        // DrawSphere((Vector3){x, y, z}, side_len, p->color);
                        DrawCubeWires((Vector3){x, y, z}, side_len, side_len,
                                      side_len, points.colors[i]);
                    }
                }
            }
        }
        EndMode3D();
        sprintf(count_text, "spheres: %d\nfps: %d", point_count, GetFPS());
        DrawText(count_text, 5, 5, 15, SKYBLUE);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}

/*
TODO list:
 - create different function to determine side length of cubes (rather than
   distance) to give it more of a bullet/beam flavor
 - optimization(?): through tweaking, find max range for cubes, only iterate
   over those per point
   - or just anything that doesn't go through every cube for every point (more
     localized rendering strategies)
 - port over to shader code maybe?

known bugs:
 - cube blobs don't fade out properly, just pop out of existence (hits border
   before out of render view)
 - some cubes jitter in and out, especially at beginning
 - point is centered BETWEEN two grid points, instead of being on a line

*/