#include <stdint.h>

#define ARENA_IMPLEMENTATION
#include "arena.h"
#include "raylib.h"

#define CUBE_SIZE 0.5f
#define PADDING 10.0f
#define SPEED 5.0f
#define XOR_MAGIC 0x2545F4914F6CDD1D
#define DEFAULT_SEED 12345
#define X_MIN 50.0f
#define X_MAX 100.0f
#define Y_MIN -30.0f
#define Y_MAX 30.0f
#define Z_MIN -30.0f
#define Z_MAX 30.0f

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
    float timer_secs;
    int is_spawned;
    struct Point *next;
} Point;

typedef struct PointActiveArray {
    int count;
    Point **array; // array of pointers to points.. uh....
} PointActiveArray;

typedef struct PointFreeList {
    Point *head;
    Point *tail;
} PointFreeList;

static inline float dir_to_start_pos(Point *p) {
    float *xyz = (float *)&p->position;
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

float *get_dir_coord_ptr(Point *p) {
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

static inline int is_negative_dir(Point *p) {
    return p->direction & (X_NEGATIVE | Y_NEGATIVE | Z_NEGATIVE);
}

Point *new_point(Arena *arena, PointFreeList *frie) {
    Point *p = arena_alloc(arena, sizeof(Point));
    p->is_spawned = 0;
    return p;
}

void reset_point(Point *p) {}

void update_point(Point *p, float dt) {
    float *override = get_dir_coord_ptr(p);
    float negative =
        p->direction & (X_NEGATIVE | Y_NEGATIVE | Z_NEGATIVE) ? -1.0f : 1.0f;
    *override += negative * SPEED * dt;
}

void free_point(PointFreeList *frie, Point *p) {
    p->is_spawned = 0;
    if (!frie->head) {
        frie->head = p;
        frie->tail = p;
    } else {
        frie->tail->next = p;
        frie->tail = p;
    }
}

void collect_free_points_and_bounds_check(PointActiveArray *points, PointFreeList *frie) {
    for (int i = 0; i < points->count; i++) {
        Point *p = points->array[i];
        if (!p->is_spawned || p->position.x <= X_MIN ||
            p->position.x >= X_MAX || p->position.y <= Y_MIN ||
            p->position.y >= Y_MAX || p->position.z <= Z_MIN ||
            p->position.z >= Z_MAX)
            free_point(frie, p);
    }
}

Point *spawn_point(PointFreeList *frie) {
    if (!frie->head)
        return NULL;
    Point *p = frie->head;
    frie->head = frie->head->next;

    p->is_spawned = 1;

    p->position = (Vector3){next_randf(X_MIN, X_MAX), next_randf(Y_MIN, Y_MAX),
                            next_randf(Z_MIN, Z_MAX)};
    uint32_t color = next_rand() | 0xFF000000;
    p->color = *(Color *)&color;
    p->timer_secs = next_randf(0.5f, 3.0f);

    p->direction = 1 << (next_rand() % DIRECTION_LEN);
    float *override = get_dir_coord_ptr(p);
    *override = dir_to_start_pos(p);
    p->next = NULL;
    return p;
}

#define SPHERE_POOL_SIZE 500
#define SPAWN_CHANCE 50

int main() {
    Arena arena = {0};

    PointActiveArray *active_points =
        arena_alloc(&arena, sizeof(PointActiveArray));
    active_points->array =
        arena_alloc(&arena, SPHERE_POOL_SIZE * sizeof(Point));

    for (int i = 0; i < SPHERE_POOL_SIZE; i++) {
        active_points->array[i] = new_point(&arena);
    }

    Camera3D camera = {.position = (Vector3){0.0f, 0.0f, 0.0f},
                       .target = (Vector3){1.0f, 0.0f, 0.0f},
                       .up = (Vector3){0.0f, 0.0f, 1.0f},
                       .fovy = 75.0f,
                       .projection = CAMERA_PERSPECTIVE};

    InitWindow(800, 600, "hi");
    float dt;
    Point *point;
    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));
    while (!WindowShouldClose()) {
        dt = GetFrameTime();
        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera);
        for (int i = 0; i < SPHERE_POOL_SIZE; i++) {
            Point *p = active_points->array[i];
            if (!p->is_spawned)
                continue;
            DrawSphere(active_points->array[i]->position, CUBE_SIZE,
                       active_points->array[i]->color);
        }
        EndMode3D();
        EndDrawing();
    }
    CloseWindow();
    arena_free(&arena);
    return 0;
}

/*
TODO list:

- get timing down (introduce random cube every 1 to 3 seconds?)


random elements:
choose direction (out of 6)


*/