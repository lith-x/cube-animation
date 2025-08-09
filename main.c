#include <math.h>
#include <stdint.h>

#define ARENA_IMPLEMENTATION
#include "arena.h"
#include "raylib.h"

#define SPHERE_SIZE 3.0f
#define PADDING 0.00001f
#define MIN_SPEED 1.0f
#define MAX_SPEED 5.0f
#define XOR_MAGIC 0x2545F4914F6CDD1D
#define DEFAULT_SEED 12345
#define X_MIN 50.0f
#define X_MAX 100.0f
#define Y_MIN -30.0f
#define Y_MAX 30.0f
#define Z_MIN -30.0f
#define Z_MAX 30.0f
#define CUBE_SPACING 4.0f
#define CUBE_SIDE_LEN 5.0f - 2 * PADDING
#define SPHERE_POOL_SIZE 15
#define MIN_SPAWN_DELAY 0.05f
#define MAX_SPAWN_DELAY 0.1f
#define EPSILON 0.1f
#define MAX_CUBE_SIZE 3.0f

// todo: feed some unique value to this at runtime
static uint32_t xorshift_state = DEFAULT_SEED;

typedef struct Point {
    Vector3 position;
    enum Direction {
        X_POSITIVE = 0x01,
        X_NEGATIVE = 0x02,
        Y_POSITIVE = 0x04,
        Y_NEGATIVE = 0x08,
        Z_POSITIVE = 0x10,
        Z_NEGATIVE = 0x20,
        DIRECTION_LEN = 6
    } direction;
    Color color;
    int is_spawned;
    float speed;
    struct Point *next;
} Point;

// typedef struct PointActiveArray {
//     int count;
//     Point **array; // array of pointers to points.. uh....
// } PointArray;

typedef struct PointFreeList {
    Point *head;
    Point *tail;
} PointFreeList;

static inline float dir_to_start_pos(Point *p) {
    switch (p->direction) {
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

static inline float *get_point_dir_xyz_ptr(Point *p) {
    float *ptr = (float *)&p->position;
    switch (p->direction) {
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
void free_point(PointFreeList *frie, Point *point) {
    point->is_spawned = 0;
    if (!frie->head) {
        frie->head = point;
        frie->tail = point;
    } else {
        frie->tail->next = point;
        frie->tail = point;
    }
}

void spawn_point_if_available(PointFreeList *frie) {
    if (!frie->head)
        return;
    Point *p = frie->head;
    frie->head = frie->head->next;

    p->is_spawned = 1;

    p->position = (Vector3){next_randf(X_MIN, X_MAX), next_randf(Y_MIN, Y_MAX),
                            next_randf(Z_MIN, Z_MAX)};
    uint32_t color = next_rand() | 0xFF000000;
    p->color = *(Color *)&color;
    p->speed = next_randf(MIN_SPEED, MAX_SPEED);

    p->direction = 1 << (next_rand() % DIRECTION_LEN);
    float *override = get_point_dir_xyz_ptr(p);
    *override = dir_to_start_pos(p);
    p->next = NULL;
}

static inline Vector3 quantize_vec(Vector3 v, float scale) {
    return (Vector3){floorf(v.x / scale) * scale, floorf(v.y / scale) * scale,
                     floorf(v.z / scale) * scale};
}

static inline float sq_distance(Vector3 a, Vector3 b) {
    return (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z);
}

int main() {
    // Arena arena = {0};

    Point points[SPHERE_POOL_SIZE] = {0};

    PointFreeList frie = {0};

    for (int i = 0; i < SPHERE_POOL_SIZE; i++) {
        free_point(&frie, &points[i]);
    }

    Camera3D camera = {.position = (Vector3){0.0f, 0.0f, 0.0f},
                       .target = (Vector3){1.0f, 0.0f, 0.0f},
                       .up = (Vector3){0.0f, 0.0f, 1.0f},
                       .fovy = 75.0f,
                       .projection = CAMERA_PERSPECTIVE};

    InitWindow(800, 600, "hi");
    float spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);
    spawn_point_if_available(&frie);
    float dt;
    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));
    char count_text[20];
    while (!WindowShouldClose()) {
        int point_count = 0;
        dt = GetFrameTime();
        spawn_timer -= dt;
        if (spawn_timer <= 0.0f) {
            spawn_point_if_available(&frie);
            spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);
        }
        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera);
        for (int i = 0; i < SPHERE_POOL_SIZE; i++) {
            Point *p = &points[i];
            if (!p->is_spawned)
                continue;
            point_count++;

            // update point position
            float *override = get_point_dir_xyz_ptr(p);
            float negative =
                p->direction & (X_NEGATIVE | Y_NEGATIVE | Z_NEGATIVE) ? -1.0f
                                                                      : 1.0f;
            *override += negative * p->speed * dt;

            // bounds check
            if (p->position.x < X_MIN || p->position.x > X_MAX ||
                p->position.y < Y_MIN || p->position.y > Y_MAX ||
                p->position.z < Z_MIN || p->position.z > Z_MAX)
                free_point(&frie, p);

            for (int z = Z_MIN; z < Z_MAX; z += CUBE_SPACING) {
                for (int y = Y_MIN; y < Y_MAX; y += CUBE_SPACING) {
                    for (int x = X_MIN; x < X_MAX; x += CUBE_SPACING) {
                        // uint32_t color = next_rand();
                        float side_len = 5.0f / sq_distance((Vector3){x, y, z}, p->position);
                        if (side_len < EPSILON) side_len = 0.0f;
                        else if (side_len > MAX_CUBE_SIZE) side_len = MAX_CUBE_SIZE;

                        DrawCubeWires((Vector3){x - side_len / 2.0f, y - side_len / 2.0f, z - side_len / 2.0f}, side_len, side_len, side_len, WHITE);
                        // DrawCubeWires(
                        //     (Vector3){x + PADDING, y + PADDING, z + PADDING},
                        //     CUBE_SIDE_LEN, CUBE_SIDE_LEN, CUBE_SIDE_LEN,
                        //     *(Color *)&color);
                    }
                }
            }
        }
        EndMode3D();
        sprintf(count_text, "%d", point_count);
        DrawText(count_text, 5, 5, 15, SKYBLUE);
        EndDrawing();
    }
    CloseWindow();
    // arena_free(&arena);
    return 0;
}

/*
TODO list:

- get timing down (introduce random cube every 1 to 3 seconds?)


random elements:
choose direction (out of 6)


*/