#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "raylib.h"

#define BULLET_POOL_SIZE 2

#define CUBE_SIZE 1.0f
#define CUBE_PADDING (CUBE_SIZE / 100.0f)
#define CUBE_SPACING (CUBE_SIZE + CUBE_PADDING)

#define CENTER ((Vector3){0.0f, 0.0f, 0.0f})

#define GRID_X 21
#define GRID_Y 21
#define GRID_Z 21

#define GRID_SCALED(x) ((x) * (float)GRID_X / CUBE_SIZE)

#define MIN_SPEED GRID_SCALED(0.1f)
#define MAX_SPEED (MIN_SPEED * 5.0f)
#define MIN_SPAWN_DELAY 0.1f
#define MAX_SPAWN_DELAY 0.5f

#define GETSIZE(x) (float)(CUBE_SPACING * (x) - CUBE_PADDING)

#define SIZE_X GETSIZE(GRID_X)
#define SIZE_Y GETSIZE(GRID_Y)
#define SIZE_Z GETSIZE(GRID_Z)

#define GRID_IDX(x, y, z) ((z) * GRID_X * GRID_Y + (y) * GRID_X + (x))

#define CUBE_COUNT (GRID_X * GRID_Y * GRID_Z)

#define EPSILON GRID_SCALED(0.002f)

#define MAX_BULLET_LENGTH GRID_SCALED(0.01f)
#define MIN_BULLET_LENGTH GRID_SCALED(0.01f)
#define MAX_BULLET_WIDTH GRID_SCALED(0.002f)
#define MIN_BULLET_WIDTH GRID_SCALED(0.002f)

#define X_MIN (CENTER.x - SIZE_X / 2.0f)
#define X_MAX (CENTER.x + SIZE_X / 2.0f)
#define Y_MIN (CENTER.y - SIZE_Y / 2.0f)
#define Y_MAX (CENTER.y + SIZE_Y / 2.0f)
#define Z_MIN (CENTER.z - SIZE_Z / 2.0f)
#define Z_MAX (CENTER.z + SIZE_Z / 2.0f)

#define LIST_END_MARK (size_t)(BULLET_POOL_SIZE)
#define SPAWNED_MARK (size_t)(BULLET_POOL_SIZE + 1)

// --------------------- RNG ---------------------

// todo: feed some unique value to this at runtime?
#define DEFAULT_SEED 12345
#define XOR_MAGIC 0x2545F4914F6CDD1D
static uint32_t xorshift_state = DEFAULT_SEED;

// xorshift* https://rosettacode.org/wiki/Pseudo-random_numbers/Xorshift_star#C
uint32_t next_rand() {
    uint32_t x = xorshift_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    xorshift_state = x;
    return x;
}

// --------------------- STRUCTS ---------------------

typedef struct Points {
    Vector3 positions[BULLET_POOL_SIZE];           // CPU/GPU
    Vector3 scales[BULLET_POOL_SIZE];              // GPU
    Vector3 inv_scales[BULLET_POOL_SIZE];          // TODO: think here
    Color colors[BULLET_POOL_SIZE];                // GPU
    float speeds[BULLET_POOL_SIZE];                // CPU
    size_t next_free_or_spawned[BULLET_POOL_SIZE]; // CPU
    enum Direction {                               // CPU
        X_POSITIVE = (uint8_t)0x01,
        X_NEGATIVE = (uint8_t)0x02,
        Y_POSITIVE = (uint8_t)0x04,
        Y_NEGATIVE = (uint8_t)0x08,
        Z_POSITIVE = (uint8_t)0x10,
        Z_NEGATIVE = (uint8_t)0x20,
        DIRECTION_LEN = 6
    } directions[BULLET_POOL_SIZE];
} Points;

typedef struct BulletFreeList {
    size_t head_idx;
    size_t tail_idx;
} BulletFreeList;

// --------------------- INLINE UTIL FN'S ---------------------

static inline float next_randf(float min, float max) {
    return min + (max - min) * (float)next_rand() / (float)UINT32_MAX;
}

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

static inline int get_direction_sign(int dir) {
    return dir & (X_POSITIVE | Y_POSITIVE | Z_POSITIVE) ? 1.0f : -1.0f;
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

static inline int get_movement_idx(int dir) {
    return dir & (X_POSITIVE | X_NEGATIVE)   ? 0
           : dir & (Y_POSITIVE | Y_NEGATIVE) ? 1
                                             : 2;
}

static inline Matrix ToTranslationMatrix(Vector3 v) {
    return (Matrix){1.0f, 0.0f, 0.0f, v.x, 0.0f, 1.0f, 0.0f, v.y,
                    0.0f, 0.0f, 1.0f, v.z, 0.0f, 0.0f, 0.0f, 1.0f};
}

static inline int get_cube_num(float pos, float min_val) {
    return (int)((pos - min_val) / CUBE_SPACING);
}

static inline float snap_val_to_grid(float pos, float min_val) {
    return (float)get_cube_num(pos, min_val) * CUBE_SPACING + min_val;
}

// --------------------- FREELIST/SPAWN HANDLING ---------------------

void init_freelist(BulletFreeList *frie, Points *points) {
    for (size_t i = 0; i < BULLET_POOL_SIZE - 1; i++)
        points->next_free_or_spawned[i] = i + 1;
    points->next_free_or_spawned[BULLET_POOL_SIZE - 1] = LIST_END_MARK;
    frie->head_idx = 0;
    frie->tail_idx = BULLET_POOL_SIZE - 1;
}

int alloc_bullet(BulletFreeList *frie, Points *points) {
    if (frie->head_idx == LIST_END_MARK)
        return LIST_END_MARK;
    int idx = frie->head_idx;
    frie->head_idx = points->next_free_or_spawned[idx];
    if (frie->head_idx == LIST_END_MARK)
        frie->tail_idx = LIST_END_MARK;
    points->next_free_or_spawned[idx] = SPAWNED_MARK;
    return idx;
}

void free_bullet(BulletFreeList *frie, Points *points, int idx) {
    points->positions[idx] = (Vector3){
        FLT_MAX, FLT_MAX, FLT_MAX}; // prevent random background stutters
    points->next_free_or_spawned[idx] = LIST_END_MARK;
    if (frie->tail_idx != LIST_END_MARK)
        points->next_free_or_spawned[frie->tail_idx] = idx;
    else
        frie->head_idx = idx;
    frie->tail_idx = idx;
}

void spawn_bullet_if_available(BulletFreeList *frie, Points *points) {
    int idx = alloc_bullet(frie, points);
    if (idx == LIST_END_MARK)
        return;

    points->colors[idx] =
        ColorLerp((Color){0xC7, 0x51, 0x08, 0xFF},
                  (Color){0x61, 0x0C, 0xCF, 0xFF}, next_randf(0.0f, 1.0f));

    points->speeds[idx] = next_randf(MIN_SPEED, MAX_SPEED);

    points->directions[idx] = 1 << (next_rand() % DIRECTION_LEN);

    float bullet_width = next_randf(MIN_BULLET_WIDTH, MAX_BULLET_WIDTH);
    points->scales[idx] = (Vector3){bullet_width, bullet_width, bullet_width};

    float inv_width = 1.0f / bullet_width;
    points->inv_scales[idx] = (Vector3){inv_width, inv_width, inv_width};

    points->positions[idx] =
        (Vector3){snap_val_to_grid(next_randf(X_MIN, X_MAX), X_MIN),
                  snap_val_to_grid(next_randf(Y_MIN, Y_MAX), Y_MIN),
                  snap_val_to_grid(next_randf(Z_MIN, Z_MAX), Z_MIN)};

    // grab which of {x,y,z} we need, then use it to point into any Vector3
    // break Vector3 down into a float pointer where +0 = x, +1 = y, +2 = z
    int xyz_idx = get_movement_idx(points->directions[idx]);

    float *scale_move_axis = &((float *)&points->scales[idx])[xyz_idx];
    *scale_move_axis = next_randf(MIN_BULLET_LENGTH, MAX_BULLET_LENGTH);

    ((float *)&points->inv_scales[idx])[xyz_idx] = 1.0f / *scale_move_axis;

    ((float *)&points->positions[idx])[xyz_idx] =
        dir_to_start_pos(points, idx) -
        get_direction_sign(points->directions[idx]) * *scale_move_axis;
}

// --------------------- MAIN ---------------------

int main() {
    Points points = {0};

    BulletFreeList frie = {0};
    init_freelist(&frie, &points);

    Vector3 cube_positions[CUBE_COUNT];
    for (int z = 0; z < GRID_Z; z++) {
        for (int y = 0; y < GRID_Y; y++) {
            for (int x = 0; x < GRID_X; x++) {
                float wx = X_MIN + x * (CUBE_SIZE + CUBE_PADDING);
                float wy = Y_MIN + y * (CUBE_SIZE + CUBE_PADDING);
                float wz = Z_MIN + z * (CUBE_SIZE + CUBE_PADDING);
                cube_positions[GRID_IDX(x, y, z)] = (Vector3){wx, wy, wz};
            }
        }
    }
    float cam_dist = CENTER.x + fmaxf(SIZE_X, fmaxf(SIZE_Y, SIZE_Z)) * 3.5f;
    Vector3 camera_pos = {cam_dist, CENTER.y, CENTER.z};
    Vector3 target_pos = CENTER;
    // cross product: (target_pos - camera_pos) x {0, 1, 0}
    Vector3 up_dir = {camera_pos.z - target_pos.z, 0.0f,
                      target_pos.x - camera_pos.x};
    Camera3D camera = {.position = camera_pos,
                       .target = target_pos,
                       .up = up_dir,
                       .fovy = 30.0f,
                       .projection = CAMERA_PERSPECTIVE};

    float dt;
    float spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);
    spawn_bullet_if_available(&frie, &points);
    char count_text[50];
    InitWindow(800, 600, "hi");
    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));

    // Mesh cube = GenMeshCube(20.0f, 20.0f, 20.0f);
    // Material mat = LoadMaterialDefault();
    // mat.shader = LoadShader("cubegrid.vs", "cubegrid.fs");

    // Matrix transforms[CUBE_COUNT];
    // for (int i = 0; i < CUBE_COUNT; i++) {
    //     transforms[i] = ToTranslationMatrix(cubePositions[i]);
    // }

    while (!WindowShouldClose()) {
        int bullet_count = 0;
        int cube_count = 0;
        dt = GetFrameTime();
        spawn_timer -= dt;
        if (spawn_timer <= 0.0f) {
            spawn_bullet_if_available(&frie, &points);
            spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);
        }
        BeginDrawing();
        UpdateCamera(&camera, CAMERA_ORBITAL);
        ClearBackground(BLACK);

        BeginMode3D(camera);
        // cube for debugging, see what i'm looking at, keep scale
        DrawCube(camera.target, CUBE_SIZE, CUBE_SIZE, CUBE_SIZE, RED);

        for (int i = 0; i < BULLET_POOL_SIZE; i++) {
            if (points.next_free_or_spawned[i] != SPAWNED_MARK)
                continue;
            bullet_count++;

            // update point position
            float *pos_move_axis =
                get_movement_axis(&points.positions[i], points.directions[i]);
            *pos_move_axis += get_direction_sign(points.directions[i]) *
                              points.speeds[i] * dt;

            float bullet_min_x =
                points.positions[i].x - points.scales[i].x - CUBE_SIZE;
            float bullet_max_x =
                points.positions[i].x + points.scales[i].x + CUBE_SIZE;
            float bullet_min_y =
                points.positions[i].y - points.scales[i].y - CUBE_SIZE;
            float bullet_max_y =
                points.positions[i].y + points.scales[i].y + CUBE_SIZE;
            float bullet_min_z =
                points.positions[i].z - points.scales[i].z - CUBE_SIZE;
            float bullet_max_z =
                points.positions[i].z + points.scales[i].z + CUBE_SIZE;

            // bounds check
            if (bullet_max_x < X_MIN || bullet_min_x > X_MAX ||
                bullet_max_y < Y_MIN || bullet_min_y > Y_MAX ||
                bullet_max_z < Z_MIN || bullet_min_z > Z_MAX) {
                free_bullet(&frie, &points, i);
                continue;
            }

            int cube_idx_min_x =
                get_cube_num(fmaxf(bullet_min_x, X_MIN), X_MIN);
            int cube_idx_max_x =
                get_cube_num(fminf(bullet_max_x, X_MAX), X_MIN) + 1;
            int cube_idx_min_y =
                get_cube_num(fmaxf(bullet_min_y, Y_MIN), Y_MIN);
            int cube_idx_max_y =
                get_cube_num(fminf(bullet_max_y, Y_MAX), Y_MIN) + 1;
            int cube_idx_min_z =
                get_cube_num(fmaxf(bullet_min_z, Z_MIN), Z_MIN);
            int cube_idx_max_z =
                get_cube_num(fminf(bullet_max_z, Z_MAX), Z_MIN) + 1;
            for (int z = cube_idx_min_z; z <= cube_idx_max_z; z++) {
                for (int y = cube_idx_min_y; y <= cube_idx_max_y; y++) {
                    for (int x = cube_idx_min_x; x <= cube_idx_max_x; x++) {
                        Vector3 *cube_pos = &cube_positions[GRID_IDX(x, y, z)];
                        float dx = fabsf((cube_pos->x - points.positions[i].x));
                        float dy = fabsf((cube_pos->y - points.positions[i].y));
                        float dz = fabsf((cube_pos->z - points.positions[i].z));
                        float dist = dx + dy + dz;
                        float side_len =
                            CUBE_SIZE * (1.0f - dist / MAX_BULLET_LENGTH);
                        if (side_len < EPSILON)
                            continue;
                        cube_count++;
                        DrawCubeWires(*cube_pos, side_len, side_len, side_len,
                                      points.colors[i]);
                    }
                }
            }
            DrawSphere((Vector3)points.positions[i], CUBE_SIZE * 0.25f,
                       points.colors[i]);
        }
        // draw the border
        DrawCube((Vector3){X_MAX, Y_MIN, Z_MIN}, 1.0f, 1.0f, 1.0f, WHITE);
        DrawCube((Vector3){X_MAX, Y_MAX, Z_MIN}, 1.0f, 1.0f, 1.0f, WHITE);
        DrawCube((Vector3){X_MAX, Y_MIN, Z_MAX}, 1.0f, 1.0f, 1.0f, WHITE);
        DrawCube((Vector3){X_MAX, Y_MAX, Z_MAX}, 1.0f, 1.0f, 1.0f, WHITE);
        DrawCube((Vector3){X_MIN, Y_MIN, Z_MIN}, 1.0f, 1.0f, 1.0f, WHITE);
        DrawCube((Vector3){X_MIN, Y_MAX, Z_MIN}, 1.0f, 1.0f, 1.0f, WHITE);
        DrawCube((Vector3){X_MIN, Y_MIN, Z_MAX}, 1.0f, 1.0f, 1.0f, WHITE);
        DrawCube((Vector3){X_MIN, Y_MAX, Z_MAX}, 1.0f, 1.0f, 1.0f, WHITE);
        EndMode3D();

        sprintf(count_text, "spheres: %d\nfps: %d\ncubes: %d", bullet_count,
                GetFPS(), cube_count);
        DrawText(count_text, 5, 5, 15, SKYBLUE);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}

/*
TODO list:
 - port over to shader code
 - blend colors for intersecting bullets, perhaps

*/