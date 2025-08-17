#include "raylib.h"
#include <GLES3/gl3.h>
#define RLGL_IMPLEMENTATION
#define GRAPHICS_API_OPENGL_ES3
#include "rlgl.h"

int rl_lines_method(void) {
    InitWindow(800, 600, "rlgl single line test");

    Camera3D camera = {.position = {0.0f, 0.0f, 5.0f},
                       .target = {0.0f, 0.0f, 0.0f},
                       .up = {0.0f, 1.0f, 0.0f},
                       .fovy = 45.0f,
                       .projection = CAMERA_PERSPECTIVE};

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera);

        // --- rlgl immediate mode line ---
        rlBegin(RL_LINES);
        rlColor3f(1.0f, 0.0f, 0.0f); // red
        rlVertex3f(-1.0f, 0.0f, 0.0f);
        rlVertex3f(1.0f, 0.0f, 0.0f);
        rlEnd();

        EndMode3D();

        EndDrawing();
    }

    CloseWindow();
    return 0;
}

// Vertex shader source
const char *vsSource = "#version 300 es\n"
                       "layout (location=0) in vec3 aPos;\n"
                       "void main() {\n"
                       "   gl_Position = vec4(aPos, 1.0);\n"
                       "}\n";

// Fragment shader source
const char *fsSource = "#version 300 es\n"
                       "precision mediump float;\n"
                       "out vec4 FragColor;\n"
                       "void main() {\n"
                       "   FragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
                       "}\n";

int vao_vbo_method(void) {
    InitWindow(800, 600, "hi");

    // --- Compile shaders ---
    Shader shader = LoadShaderFromMemory(vsSource, fsSource);

    // --- Setup line geometry ---
    float vertices[] = {0.5f, 0.5f, 0.0f,  //
                        0.5f, -0.5f, 0.0f, //
                        //
                        0.5f, -0.5f, 0.0f,  //
                        -0.5f, -0.5f, 0.0f, //
                        //
                        -0.5f, -0.5f, 0.0f, //
                        -0.5f, 0.5f, 0.0f,  //
                        //
                        -0.5f, 0.5f, 0.0f, //
                        0.5f, 0.5f, 0.0f};

    uint32_t VAO = rlLoadVertexArray();
    rlEnableVertexArray(VAO);

    uint32_t VBO = rlLoadVertexBuffer(vertices, sizeof(vertices), false);
    rlSetVertexAttribute(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), 0);
    rlEnableVertexAttribute(0);

    // rlDisableVertexBuffer(); // might be necessary if we're loading more
    // stuff
    // rlDisableVertexArray();

    // --- Main loop ---
    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground(BLACK);

        rlEnableShader(shader.id);
        // BeginShaderMode(shader);
        rlEnableVertexArray(VAO);
        glDrawArrays(GL_LINES, 0, 8);
        // EndShaderMode();
        EndDrawing();
    }

    // Cleanup
    rlUnloadVertexArray(VAO);
    rlUnloadVertexBuffer(VBO);
    rlUnloadShaderProgram(shader.id);

    CloseWindow();
    return 0;
}

int ogl_tutorial() {
    InitWindow(800, 600, "hi");
    GLuint vbo;
    Vector3 point = {0};
    glGenBuffers(1, &vbo);

    while (!WindowShouldClose()) {
        BeginDrawing();
        EndDrawing();
    }
    CloseWindow();
    return 0;
}

int main() { return ogl_tutorial(); }

/*
Trying to keep my head on straight, just doing this:
- Basically, mimic DrawMeshInstanced(), except that function at its core
  utilizes GL_TRIANGLES, I need GL_LINES.


*/