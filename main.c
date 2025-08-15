#include <float.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "raylib.h"

// ----------- ~%~ macros ~%~ -----------

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 800

// bullet constants
#define BULLET_POOL_SIZE 5
static const float MIN_SPEED = 0; // todo: figure this shit out
static const float MAX_SPEED = 0;

// we're good here, no scaling necessary
static const float MIN_SPAWN_DELAY = 0.1f;
static const float MAX_SPAWN_DELAY = 0.2f;

static const float MIN_BULLET_RADIUS = 0;
static const float MAX_BULLET_RADIUS = 0;

static const float MIN_BULLET_LEN = 0;
static const float MAX_BULLET_LEN = 0;

#define FREELIST_END BULLET_POOL_SIZE
#define IS_SPAWNED (BULLET_POOL_SIZE + 1)

// ----------- ~%~ structs ~%~ -----------

typedef struct Bullets {
    Vector3 positions[BULLET_POOL_SIZE];
    Color colors[BULLET_POOL_SIZE];
    Vector3 scales[BULLET_POOL_SIZE];
    float speeds[BULLET_POOL_SIZE];
    size_t next_free_or_spawned[BULLET_POOL_SIZE];
    enum Direction {
        PX = 0x01,
        NX = 0x02,
        PY = 0x04,
        NY = 0x08,
        PZ = 0x10,
        NZ = 0x20,
        DIR_LEN = 6
    } directions[BULLET_POOL_SIZE];
} Bullets;

typedef struct Freelist {
    size_t head;
    size_t tail;
} Freelist;

// ----------- ~%~ helper fn's ~%~ -----------

// todo: feed some unique value to this at runtime?
#define DEFAULT_SEED 12345
#define XOR_MAGIC 0x2545F4914F6CDD1D
static uint32_t xorshift_state = DEFAULT_SEED;

// xorshift32
uint32_t next_rand() {
    uint32_t x = xorshift_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    xorshift_state = x;
    return x;
}

static inline float next_randf(float min, float max) {
    return min + (max - min) * (float)next_rand() / (float)UINT32_MAX;
}

static inline int get_xyz(int dir) {
    return dir & (PX | NX) ? 0 : dir & (PY | NY) ? 1 : 2;
}

static inline float get_sign(int dir) {
    return dir & (PX | PY | PZ) ? 1.0f : -1.0f;
}

// ----------- ~%~ spawn logic ~%~ -----------

static inline void init_freelist(Freelist *frie, Bullets *bullets) {
    for (size_t i = 0; i < BULLET_POOL_SIZE - 1; i++)
        bullets->next_free_or_spawned[i] = i + 1;
    bullets->next_free_or_spawned[BULLET_POOL_SIZE - 1] = FREELIST_END;
    frie->head = 0;
    frie->tail = BULLET_POOL_SIZE - 1;
}

void free_bullet(Freelist *frie, Bullets *bullets, int idx) {
    bullets->positions[idx] = (Vector3){
        FLT_MAX, FLT_MAX, FLT_MAX}; // prevent random background stutters
    bullets->next_free_or_spawned[idx] = FREELIST_END;
    if (frie->tail != FREELIST_END)
        bullets->next_free_or_spawned[frie->tail] = idx;
    else
        frie->head = idx;
    frie->tail = idx;
}

void spawn_bullet(Freelist *frie, Bullets *bullets) {
    // get next free bullet, if one is available
    if (frie->head == FREELIST_END)
        return;
    size_t idx = frie->head;
    frie->head = bullets->next_free_or_spawned[idx];
    if (frie->head == FREELIST_END)
        frie->tail = FREELIST_END;
    bullets->next_free_or_spawned[idx] = IS_SPAWNED;

    // initialize bullet data
    bullets->colors[idx] =
        ColorLerp((Color){0xC7, 0x51, 0x08, 0xFF},
                  (Color){0x61, 0x0C, 0xCF, 0xFF}, next_randf(0.0f, 1.0f));
    bullets->directions[idx] = 1 << (next_rand() % DIR_LEN);

    bullets->speeds[idx] = next_randf(MIN_SPEED, MAX_SPEED);
    float bullet_radius = next_randf(MIN_BULLET_RADIUS, MAX_BULLET_RADIUS);
    bullets->scales[idx] =
        (Vector3){bullet_radius, bullet_radius, bullet_radius};
    bullets->positions[idx] = (Vector3){0}; // todo: this, with scaling in mind

    // grab 0,1,2 so when we cast Vector3 to float pointer we can point to the
    // only axis we care about across multiple vectors
    int xyz_idx = get_xyz(bullets->directions[idx]);
    ((float *)&bullets->scales[idx])[xyz_idx] =
        next_randf(MIN_BULLET_LEN, MAX_BULLET_LEN);
}

// ----------- ~%~ main ~%~ -----------

int main() {
    // setup data
    Bullets bullets = {0};
    Freelist frie = {0};
    init_freelist(&frie, &bullets);

    float dt = 0, spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);

    char debug_text[100];

    // todo: make resizeable, use window_size as source of truth
    Vector2 window_size = {800, 600};
    InitWindow(window_size.x, window_size.y, "hi");
    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));

    while (!WindowShouldClose()) {
        dt = GetFrameTime();
        if ((spawn_timer -= dt) <= 0.0f) {
            spawn_bullet(&frie, &bullets);
            spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);
        }
        BeginDrawing();
        ClearBackground(BLACK);
        int bullet_count = 0;
        for (int i = 0; i < BULLET_POOL_SIZE; i++) {
            if (bullets.next_free_or_spawned[i] != IS_SPAWNED)
                continue;
            bullet_count++;

            int dir = bullets.directions[i];
            ((float *)&bullets.positions[i])[get_xyz(dir)] +=
                get_sign(dir) * bullets.speeds[i] * dt;
        }
        sprintf(debug_text, "bullets: %d", bullet_count);
        DrawText(debug_text, 5, 5, 16, SKYBLUE);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}


/*

Index Space -> World Space



*/