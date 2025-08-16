#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "raymath.h"

// ----------- ~%~ macros ~%~ -----------

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 800

#define CENTER ((Vector3){0.0f, 0.0f, 0.0f})

// cube constants
#define CUBE_SIZE 1.0f
#define CUBE_PADDING 0.1f

#define CUBES_X 25
#define CUBES_Y 25
#define CUBES_Z 25
#define CUBES_COUNT (CUBES_X * CUBES_Y * CUBES_Z)

// note: SIZE_X is the scaling factor for bullet speed, radius, etc.
#define GETLENGTH(x) ((CUBE_SIZE + CUBE_PADDING) * (x) - CUBE_PADDING)
#define SIZE_X GETLENGTH(CUBES_X)
#define SIZE_Y GETLENGTH(CUBES_Y)
#define SIZE_Z GETLENGTH(CUBES_Z)

#define X_MIN (CENTER.x - SIZE_X / 2.0f)
#define X_MAX (CENTER.x + SIZE_X / 2.0f)
#define Y_MIN (CENTER.y - SIZE_Y / 2.0f)
#define Y_MAX (CENTER.y + SIZE_Y / 2.0f)
#define Z_MIN (CENTER.z - SIZE_Z / 2.0f)
#define Z_MAX (CENTER.z + SIZE_Z / 2.0f)

#define X_MIN_CUBE_CENTER (X_MIN + CUBE_SIZE / 2.0f)
#define X_MAX_CUBE_CENTER (X_MAX - CUBE_SIZE / 2.0f)
#define Y_MIN_CUBE_CENTER (Y_MIN + CUBE_SIZE / 2.0f)
#define Y_MAX_CUBE_CENTER (Y_MAX - CUBE_SIZE / 2.0f)
#define Z_MIN_CUBE_CENTER (Z_MIN + CUBE_SIZE / 2.0f)
#define Z_MAX_CUBE_CENTER (Z_MAX - CUBE_SIZE / 2.0f)

#define CUBE_IDX(x, y, z) (CUBES_X * CUBES_Y * (z) + CUBES_X * (y) + (x))

// bullet constants
#define BULLET_POOL_SIZE 32
static const float MIN_SPEED = SIZE_X / 5.0f;
static const float MAX_SPEED = MIN_SPEED * 2.0f;

// we're good here, no scaling necessary
static const float MIN_SPAWN_DELAY = 0.01f;
static const float MAX_SPAWN_DELAY = 0.1f;

// how many
static const float MIN_BULLET_RADIUS = SIZE_X / 15.0f;
static const float MAX_BULLET_RADIUS = MIN_BULLET_RADIUS * 2.0f;

static const float MIN_BULLET_LEN = SIZE_X / 5.0f;
static const float MAX_BULLET_LEN = MIN_BULLET_LEN * 2.0f;

#define FREELIST_END BULLET_POOL_SIZE
#define IS_SPAWNED (BULLET_POOL_SIZE + 1)

// ----------- ~%~ structs ~%~ -----------

typedef struct Bullets {
    Vector3 positions[BULLET_POOL_SIZE];
    Vector4 colors[BULLET_POOL_SIZE];
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

typedef struct BulletBox {
    int min_x, max_x, min_y, max_y, min_z, max_z;
} BulletBox;

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

// todo: refactor so we include the scale offset here, will probably look nicer
static inline float get_start_pos(int dir) {
    switch (dir) {
    case NX:
        return X_MAX;
    case PX:
        return X_MIN;
    case NY:
        return Y_MAX;
    case PY:
        return Y_MIN;
    case NZ:
        return Z_MAX;
    case PZ:
        return Z_MIN;
    default:
        __builtin_unreachable();
    }
}

static inline float get_random_grid_pos(int dim_count, float min_val) {
    return min_val +
           (float)(next_rand() % dim_count) * (CUBE_SIZE + CUBE_PADDING);
}

static inline int is_out_of_bounds(Vector3 pos, Vector3 scale, int dir) {
    switch (dir) {
    case PX:
        return pos.x > (X_MAX + scale.x);
    case NX:
        return pos.x < (X_MIN - scale.x);
    case PY:
        return pos.y > (Y_MAX + scale.y);
    case NY:
        return pos.y < (Y_MIN - scale.y);
    case PZ:
        return pos.z > (Z_MAX + scale.z);
    case NZ:
        return pos.z < (Z_MIN - scale.z);
    }
    return 1;
}

static inline int world_to_index(float coord, float base_pos, int max_idx) {
    // NOTE: idk why ceilf had to be used here but it fixed an off by -1 offset
    // issue.
    int ret = (int)ceilf((coord - base_pos) / (CUBE_SIZE + CUBE_PADDING));
    return ret > max_idx ? max_idx : ret < 0 ? 0 : ret;
}

static inline BulletBox get_bullet_bounding_box(Vector3 pos, Vector3 scale) {
    return (BulletBox){
        .min_x = world_to_index(pos.x - scale.x, X_MIN_CUBE_CENTER, CUBES_X),
        .max_x = world_to_index(pos.x + scale.x, X_MIN_CUBE_CENTER, CUBES_X),
        .min_y = world_to_index(pos.y - scale.y, Y_MIN_CUBE_CENTER, CUBES_Y),
        .max_y = world_to_index(pos.y + scale.y, Y_MIN_CUBE_CENTER, CUBES_Y),
        .min_z = world_to_index(pos.z - scale.z, Z_MIN_CUBE_CENTER, CUBES_Z),
        .max_z = world_to_index(pos.z + scale.z, Z_MIN_CUBE_CENTER, CUBES_Z),
    };
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

// returns index if bullet is spawned, BULLET_POOL_SIZE if not
int spawn_bullet(Freelist *frie, Bullets *bullets) {
    // get next free bullet, if one is available, bail otherwise
    if (frie->head == FREELIST_END)
        return FREELIST_END;
    size_t idx = frie->head;
    frie->head = bullets->next_free_or_spawned[idx];
    if (frie->head == FREELIST_END)
        frie->tail = FREELIST_END;
    bullets->next_free_or_spawned[idx] = IS_SPAWNED;

    // initialize bullet data

    bullets->colors[idx] = Vector4Lerp(
        (Vector4){0xC7 / 255.0f, 0x51 / 255.0f, 0x08 / 255.0f, 1.0f},
        (Vector4){0x61 / 255.0f, 0x0C / 255.0f, 0xCF / 255.0f, 1.0f},
        next_randf(0.0f, 1.0f));
    bullets->directions[idx] = 1 << (next_rand() % DIR_LEN);

    bullets->speeds[idx] = next_randf(MIN_SPEED, MAX_SPEED);
    float bullet_radius = next_randf(MIN_BULLET_RADIUS, MAX_BULLET_RADIUS);
    bullets->scales[idx] =
        (Vector3){bullet_radius, bullet_radius, bullet_radius};
    bullets->positions[idx] =
        (Vector3){get_random_grid_pos(CUBES_X, X_MIN_CUBE_CENTER),
                  get_random_grid_pos(CUBES_Y, Y_MIN_CUBE_CENTER),
                  get_random_grid_pos(CUBES_Z, Z_MIN_CUBE_CENTER)};

    // grab x,y,z offset so we can point to the relevant axis across multiple
    // Vector3's when they're casted to float*
    int xyz_idx = get_xyz(bullets->directions[idx]);

    float *scale_xyz = &((float *)&bullets->scales[idx])[xyz_idx];
    *scale_xyz = next_randf(MIN_BULLET_LEN, MAX_BULLET_LEN);

    ((float *)&bullets->positions[idx])[xyz_idx] =
        get_start_pos(bullets->directions[idx]) -
        (*scale_xyz) * get_sign(bullets->directions[idx]);
    return idx;
}

// ----------- ~%~ main ~%~ -----------

int cpu_render() {
    // setup data
    Bullets bullets = {0};
    Freelist frie = {0};
    init_freelist(&frie, &bullets);

    // TODO: change this to directly build transforms
    Vector3 cube_positions[CUBES_COUNT];
    Vector3 ref_pos = {0.0f, 0.0f, Z_MIN_CUBE_CENTER};
    for (int z = 0; z < CUBES_Z; z++) {
        ref_pos.y = Y_MIN_CUBE_CENTER;
        for (int y = 0; y < CUBES_Y; y++) {
            ref_pos.x = X_MIN_CUBE_CENTER;
            for (int x = 0; x < CUBES_X; x++) {
                cube_positions[CUBE_IDX(x, y, z)] = ref_pos;
                ref_pos.x += CUBE_SIZE + CUBE_PADDING;
            }
            ref_pos.y += CUBE_SIZE + CUBE_PADDING;
        }
        ref_pos.z += CUBE_SIZE + CUBE_PADDING;
    }

    float dt = 0, spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);
    char debug_text[256];

    Camera3D camera = {.position = {400.0f, 0.0f, 0.0f},
                       .target = CENTER,
                       .up = {0.0f, 1.0f, 0.0f},
                       .fovy = 5.0f,
                       .projection = CAMERA_PERSPECTIVE};

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
        // debug: keep track of bullet count
        int bullet_count = 0;
        // debug: visualize cube grid
        // for (int i = 0; i < CUBES_COUNT; i++) {
        //     DrawPoint3D(cube_positions[i], WHITE);
        // }

        // UpdateCamera(&camera, CAMERA_ORBITAL);
        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(camera);
        for (int i = 0; i < BULLET_POOL_SIZE; i++) {
            if (bullets.next_free_or_spawned[i] != IS_SPAWNED)
                continue;
            bullet_count++;

            int dir = bullets.directions[i];
            ((float *)&bullets.positions[i])[get_xyz(dir)] +=
                get_sign(dir) * bullets.speeds[i] * dt;
            if (is_out_of_bounds(bullets.positions[i], bullets.scales[i],
                                 dir)) {
                free_bullet(&frie, &bullets, i);
                continue;
            }
            // debug: visualize bullet positions
            // DrawSphereEx(bullets.positions[i], CUBE_SIZE / 8.0f, 4, 4,
            //              ColorFromNormalized(bullets.colors[i]));

            // CPU RENDERING

            BulletBox bbox = get_bullet_bounding_box(bullets.positions[i],
                                                     bullets.scales[i]);
            Vector3 *bullet_pos = &bullets.positions[i];
            for (int z = bbox.min_z; z < bbox.max_z; z++) {
                for (int y = bbox.min_y; y < bbox.max_y; y++) {
                    for (int x = bbox.min_x; x < bbox.max_x; x++) {
                        Vector3 *cube_pos = &cube_positions[CUBE_IDX(x, y, z)];
                        float dx = fabsf((bullet_pos->x - cube_pos->x) /
                                         bullets.scales[i].x);
                        float dy = fabsf((bullet_pos->y - cube_pos->y) /
                                         bullets.scales[i].y);
                        float dz = fabsf((bullet_pos->z - cube_pos->z) /
                                         bullets.scales[i].z);
                        float d = dx + dy + dz;
                        float side_len = fmaxf(0.0f, CUBE_SIZE * (1 - d));
                        if (side_len <= EPSILON)
                            continue;
                        // debug: bypass side length calc, show all cubes
                        // float side_len = CUBE_SIZE;
                        DrawCubeWires(*cube_pos, side_len, side_len, side_len,
                                      ColorFromNormalized(bullets.colors[i]));
                    }
                }
            }
        }
        EndMode3D();
        // debug: show stats
        sprintf(debug_text, "bullets: %d\nfps: %d", bullet_count, GetFPS());
        DrawText(debug_text, 5, 5, 16, SKYBLUE);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}

int gpu_render() {
    // setup data
    Bullets bullets = {0};
    Freelist frie = {0};
    init_freelist(&frie, &bullets);

    // TODO: change this to directly build transforms

    Matrix transforms[CUBES_COUNT];
    Vector3 ref_pos = {0.0f, 0.0f, Z_MIN_CUBE_CENTER};
    for (int z = 0; z < CUBES_Z; z++) {
        ref_pos.y = Y_MIN_CUBE_CENTER;
        for (int y = 0; y < CUBES_Y; y++) {
            ref_pos.x = X_MIN_CUBE_CENTER;
            for (int x = 0; x < CUBES_X; x++) {
                transforms[CUBE_IDX(x,y,z)] = MatrixTranslate(ref_pos.x, ref_pos.y, ref_pos.z);
                ref_pos.x += CUBE_SIZE + CUBE_PADDING;
            }
            ref_pos.y += CUBE_SIZE + CUBE_PADDING;
        }
        ref_pos.z += CUBE_SIZE + CUBE_PADDING;
    }

    float dt = 0, spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);
    char debug_text[256];

    Camera3D camera = {.position = {400.0f, 0.0f, 0.0f},
                       .target = CENTER,
                       .up = {0.0f, 1.0f, 0.0f},
                       .fovy = 5.0f,
                       .projection = CAMERA_PERSPECTIVE};

    // todo: make resizeable, use window_size as source of truth
    Vector2 window_size = {800, 600};
    InitWindow(window_size.x, window_size.y, "hi");
    SetTargetFPS(GetMonitorRefreshRate(GetCurrentMonitor()));

    Mesh cube = GenMeshCube(1.0f, 1.0f, 1.0f);
    Model cube_model = LoadModelFromMesh(cube);

    Shader shader = LoadShader("cubegrid.vs", "cubegrid.fs");
    if (!IsShaderValid(shader)) {
        fprintf(stderr, "shader did an oopsie woopsie\n");
        exit(1);
    }
    cube_model.materials[0].shader = shader;

    while (!WindowShouldClose()) {
        dt = GetFrameTime();
        if ((spawn_timer -= dt) <= 0.0f) {
            spawn_bullet(&frie, &bullets);
            spawn_timer = next_randf(MIN_SPAWN_DELAY, MAX_SPAWN_DELAY);
        }
        
        // GPU stuff TODO: see if I can't just use bullets struct directly
        // instead of reassigning like this
        Vector3 bulletPos[BULLET_POOL_SIZE];
        Vector3 bulletScale[BULLET_POOL_SIZE];
        Vector4 bulletColor[BULLET_POOL_SIZE];
        
        int bullet_count = 0;

        for (int i = 0; i < BULLET_POOL_SIZE; i++) {
            if (bullets.next_free_or_spawned[i] != IS_SPAWNED)
                continue;
            bullet_count++;

            int dir = bullets.directions[i];
            ((float *)&bullets.positions[i])[get_xyz(dir)] +=
                get_sign(dir) * bullets.speeds[i] * dt;
            if (is_out_of_bounds(bullets.positions[i], bullets.scales[i],
                                 dir)) {
                free_bullet(&frie, &bullets, i);
                continue;
            }
            bulletPos[bullet_count] = bullets.positions[i];
            bulletScale[bullet_count] = bullets.scales[i];
            bulletColor[bullet_count] = bullets.colors[i];
        }

        SetShaderValue(shader, GetShaderLocation(shader, "uBulletCount"),
                       &bullet_count, SHADER_UNIFORM_INT);
        SetShaderValueV(shader, GetShaderLocation(shader, "uBulletPos"),
                        bulletPos, SHADER_UNIFORM_VEC3, bullet_count);
        SetShaderValueV(shader, GetShaderLocation(shader, "uBulletScale"),
                        bulletScale, SHADER_UNIFORM_VEC3, bullet_count);
        SetShaderValueV(shader, GetShaderLocation(shader, "uBulletColor"),
                        bulletColor, SHADER_UNIFORM_VEC4, bullet_count);

        // UpdateCamera(&camera, CAMERA_ORBITAL);
        BeginDrawing();
        ClearBackground(BLACK);
        BeginMode3D(camera);
        DrawMeshInstanced(cube, cube_model.materials[0], transforms,
                          CUBES_COUNT);
        EndMode3D();
        // debug: show stats
        sprintf(debug_text, "bullets: %d\nfps: %d", bullet_count, GetFPS());
        DrawText(debug_text, 5, 5, 16, SKYBLUE);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}

int main() {
    return gpu_render();
}

/*

Index Space -> World Space


Octahedron equation: |x / scale_x| + |y / scale_y| + |z / scale_z| <= 1

d = |(bul_x - cube_x) / scale_x| + |(bul_y - cube_y) / scale_y| + |(bul_z -
cube_z) / scale_z|

side_len = maxf(0.0f, CUBE_SIZE * (1 - d))

*/