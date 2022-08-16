#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <math.h>

#define GLFW_INCLUDE_GLEXT
#include <GLFW/glfw3.h>

#include "glextloader.c"

#define DEFAULT_SCREEN_WIDTH 1600
#define DEFAULT_SCREEN_HEIGHT 900
#define SEEDS_COUNT 10

typedef struct {
    float x, y;
} Vector2;

typedef struct {
    float x, y, z, w;
} Vector4;

static Vector2 seed_positions[SEEDS_COUNT];
static Vector4 seed_colors[SEEDS_COUNT];
static Vector2 seed_velocities[SEEDS_COUNT];
static double delta_time = 0.0f;

void MessageCallback(GLenum source,
                     GLenum type,
                     GLuint id,
                     GLenum severity,
                     GLsizei length,
                     const GLchar* message,
                     const void* userParam)
{
    (void) source;
    (void) id;
    (void) length;
    (void) userParam;
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
            (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
            type, severity, message);
}

char *slurp_file_into_malloced_cstr(const char *file_path)
{
    FILE *f = NULL;
    char *buffer = NULL;

    f = fopen(file_path, "r");
    if (f == NULL) goto fail;
    if (fseek(f, 0, SEEK_END) < 0) goto fail;

    long size = ftell(f);
    if (size < 0) goto fail;

    buffer = malloc(size + 1);
    if (buffer == NULL) goto fail;

    if (fseek(f, 0, SEEK_SET) < 0) goto fail;

    fread(buffer, 1, size, f);
    if (ferror(f)) goto fail;

    buffer[size] = '\0';

    if (f) {
        fclose(f);
        errno = 0;
    }
    return buffer;
fail:
    if (f) {
        int saved_errno = errno;
        fclose(f);
        errno = saved_errno;
    }
    if (buffer) {
        free(buffer);
    }
    return NULL;
}

const char *shader_type_as_cstr(GLuint shader)
{
    switch (shader) {
    case GL_VERTEX_SHADER:
        return "GL_VERTEX_SHADER";
    case GL_FRAGMENT_SHADER:
        return "GL_FRAGMENT_SHADER";
    default:
        return "(Unknown)";
    }
}

bool compile_shader_source(const GLchar *source, GLenum shader_type, GLuint *shader)
{
    *shader = glCreateShader(shader_type);
    glShaderSource(*shader, 1, &source, NULL);
    glCompileShader(*shader);

    GLint compiled = 0;
    glGetShaderiv(*shader, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        GLchar message[1024];
        GLsizei message_size = 0;
        glGetShaderInfoLog(*shader, sizeof(message), &message_size, message);
        fprintf(stderr, "ERROR: could not compile %s\n", shader_type_as_cstr(shader_type));
        fprintf(stderr, "%.*s\n", message_size, message);
        return false;
    }

    return true;
}

bool compile_shader_file(const char *file_path, GLenum shader_type, GLuint *shader)
{
    char *source = slurp_file_into_malloced_cstr(file_path);
    if (source == NULL) {
        fprintf(stderr, "ERROR: failed to read file `%s`: %s\n", file_path, strerror(errno));
        errno = 0;
        return false;
    }
    bool ok = compile_shader_source(source, shader_type, shader);
    if (!ok) {
        fprintf(stderr, "ERROR: failed to compile `%s` shader file\n", file_path);
    }
    free(source);
    return ok;
}

bool link_program(GLuint vert_shader, GLuint frag_shader, GLuint *program)
{
    *program = glCreateProgram();

    glAttachShader(*program, vert_shader);
    glAttachShader(*program, frag_shader);
    glLinkProgram(*program);

    GLint linked = 0;
    glGetProgramiv(*program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLsizei message_size = 0;
        GLchar message[1024];

        glGetProgramInfoLog(*program, sizeof(message), &message_size, message);
        fprintf(stderr, "Program Linking: %.*s\n", message_size, message);
    }

    glDeleteShader(vert_shader);
    glDeleteShader(frag_shader);

    return program;
}

bool load_shader_program(const char *vertex_file_path,
                         const char *fragment_file_path,
                         GLuint *program)
{
    GLuint vert = 0;
    if (!compile_shader_file(vertex_file_path, GL_VERTEX_SHADER, &vert)) {
        return false;
    }

    GLuint frag = 0;
    if (!compile_shader_file(fragment_file_path, GL_FRAGMENT_SHADER, &frag)) {
        return false;
    }

    if (!link_program(vert, frag, program)) {
        return false;
    }

    return true;
}

float rand_float(void)
{
    return (float) rand() / RAND_MAX;
}

float lerpf(float a, float b, float t)
{
    return a + (b - a)*t;
}

void generate_random_seeds(void)
{
    for (size_t i = 0; i < SEEDS_COUNT; ++i) {
        seed_positions[i].x = rand_float()*DEFAULT_SCREEN_WIDTH;
        seed_positions[i].y = rand_float()*DEFAULT_SCREEN_HEIGHT;
        seed_colors[i].x = rand_float();
        seed_colors[i].y = rand_float();
        seed_colors[i].z = rand_float();
        seed_colors[i].w = 1;

        float angle = rand_float()*2*M_PI;
        float mag = lerpf(100, 500, rand_float());
        seed_velocities[i].x = cosf(angle)*mag;
        seed_velocities[i].y = sinf(angle)*mag;
    }
}

float absf(float a) {
    return a < 0 ? a * -1.0f : a;
}

float maxf(float a, float b) {
    return a > b ? a : b;
}

float minf(float a, float b) {
    return a < b ? a : b;
}

float clampf(float min, float max, float val) {
    return maxf(min, minf(max, val));
}

Vector2 calculate_force_between_points(Vector2 p1, float mass1, Vector2 p2, float mass2) {
    float dx = p1.x - p2.x;
    float dy = p1.y - p2.y;

    // For "real" gravity it should be dx*dx + dy*dy, but it looks
    // better this way.
    float distance = absf(dx) + absf(dy);

    if (distance == 0)
        return (Vector2) { 0.0f, 0.0f };

    float force = (mass1 + mass2) / distance;

    return (Vector2) {
        .x = dx * force,
        .y = dy * force
    };
}

void update_positions(void) {
    for (size_t i = 0; i < SEEDS_COUNT; ++i) {
        float ax = 0.0f;
        float ay = 0.0f;
        for (size_t j = 0; j < SEEDS_COUNT; ++j) {
            if (i == j)
                continue;
#define SEED_MASS -2.0f
            Vector2 deltas = calculate_force_between_points(seed_positions[i],
                                                            SEED_MASS,
                                                            seed_positions[j],
                                                            SEED_MASS);

            ax -= deltas.x;
            ay -= deltas.y;
        }


        Vector2 corners[] = {
            (Vector2) {
                .x = 0.0f,
                .y = 0.0f
            },
                (Vector2) {
                .x = 0.0f,
                .y = (float) DEFAULT_SCREEN_HEIGHT
            },
                (Vector2) {
                .x = DEFAULT_SCREEN_WIDTH,
                .y = (float) DEFAULT_SCREEN_HEIGHT
            },
                (Vector2) {
                .x = DEFAULT_SCREEN_WIDTH,
                .y = 0.0f
            }
        };

#define CORNERS_COUNT 4
#define CORNER_MASS -30.0f

        for (int c = 0; c < CORNERS_COUNT; ++c) {
            Vector2 deltas = calculate_force_between_points(seed_positions[i],
                                                            SEED_MASS,
                                                            corners[c],
                                                            CORNER_MASS);
            ax += deltas.x;
            ay += deltas.y;
        }

        seed_velocities[i].x += ax;
        seed_velocities[i].y += ay;

#define VELOCITY_CAP 300.0f

        seed_velocities[i].x = clampf(-VELOCITY_CAP, VELOCITY_CAP, seed_velocities[i].x);
        seed_velocities[i].y = clampf(-VELOCITY_CAP, VELOCITY_CAP, seed_velocities[i].y);

        float x = seed_positions[i].x + seed_velocities[i].x*delta_time;
        if (0 <= x && x <= DEFAULT_SCREEN_WIDTH) {
            seed_positions[i].x = x;
        } else {
            seed_velocities[i].x *= -1;
        }
        float y = seed_positions[i].y + seed_velocities[i].y*delta_time;
        if (0 <= y && y <= DEFAULT_SCREEN_HEIGHT) {
            seed_positions[i].y = y;
        } else {
            seed_velocities[i].y *= -1;
        }
    }
}

int main(void)
{
    generate_random_seeds();

    if (!glfwInit()) {
        fprintf(stderr, "ERROR: could not initialize GLFW\n");
        exit(1);
    }

    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);


    GLFWwindow * const window = glfwCreateWindow(
                                    DEFAULT_SCREEN_WIDTH,
                                    DEFAULT_SCREEN_HEIGHT,
                                    "OpenGL Template",
                                    NULL,
                                    NULL);
    if (window == NULL) {
        fprintf(stderr, "ERROR: could not create a window.\n");
        glfwTerminate();
        exit(1);
    }

    int gl_ver_major = glfwGetWindowAttrib(window, GLFW_CONTEXT_VERSION_MAJOR);
    int gl_ver_minor = glfwGetWindowAttrib(window, GLFW_CONTEXT_VERSION_MINOR);
    printf("OpenGL %d.%d\n", gl_ver_major, gl_ver_minor);

    glfwMakeContextCurrent(window);

    load_gl_extensions();

    if (glDrawArraysInstanced == NULL) {
        fprintf(stderr, "Support for EXT_draw_instanced is required!\n");
        exit(1);
    }

    if (glDebugMessageCallback != NULL) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(MessageCallback, 0);
    }

    glEnable(GL_DEPTH_TEST);

    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLuint positions_vbo;
    {
        glGenBuffers(1, &positions_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, positions_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(seed_positions), seed_positions, GL_DYNAMIC_DRAW);

#define VA_POS 0
        glEnableVertexAttribArray(VA_POS);
        glVertexAttribPointer(VA_POS,
                              2,
                              GL_FLOAT,
                              GL_FALSE,
                              0,
                              (void*)0);
        glVertexAttribDivisor(VA_POS, 1);
    }

    GLuint colors_vbo;
    {
        glGenBuffers(1, &colors_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, colors_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(seed_colors), seed_colors, GL_STATIC_DRAW);

#define VA_COLOR 1
        glEnableVertexAttribArray(VA_COLOR);
        glVertexAttribPointer(VA_COLOR,
                              4,
                              GL_FLOAT,
                              GL_FALSE,
                              0,
                              (void*)0);
        glVertexAttribDivisor(VA_COLOR, 1);
    }

    const char *vertex_file_path = "shaders/quad.vert";
    const char *fragment_file_path = "shaders/color.frag";
    GLuint program;
    if (!load_shader_program(vertex_file_path, fragment_file_path, &program)) {
        exit(1);
    }
    glUseProgram(program);

    GLint u_resolution = glGetUniformLocation(program, "resolution");
    GLint u_color = glGetUniformLocation(program, "color");
    GLint u_seed = glGetUniformLocation(program, "seed");

    printf("u_resolution = %d\n", u_resolution);
    printf("u_color = %d\n", u_color);
    printf("u_seed = %d\n", u_seed);

    double prev_time = 0.0;

    glUniform2f(u_resolution, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.25f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        update_positions();

        glBindBuffer(GL_ARRAY_BUFFER, positions_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(seed_positions), seed_positions);

        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, SEEDS_COUNT);

        glfwSwapBuffers(window);
        glfwPollEvents();

        double cur_time = glfwGetTime();
        delta_time = cur_time - prev_time;
        prev_time = cur_time;
    }

    return 0;
}
