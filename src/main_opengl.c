#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <math.h>

#include <sys/stat.h>
#include <sys/types.h>

#define GLFW_INCLUDE_GLEXT
#include <GLFW/glfw3.h>

#include "glextloader.c"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define DEFAULT_SCREEN_WIDTH 1600
#define DEFAULT_SCREEN_HEIGHT 900
#define SEEDS_COUNT 20

#define UNIMPLEMENTED(message) \
    do { \
        fprintf(stderr, "%s:%d: UNIMPLEMENTED: %s\n", __FILE__, __LINE__, message); \
        exit(1); \
    } while (0)

#define UNREACHABLE(message) \
    do { \
        fprintf(stderr, "%s:%d: UNREACHABLE: %s\n", __FILE__, __LINE__, message); \
        exit(1); \
    } while (0)

typedef struct {
    float x, y;
} Vector2;

typedef struct {
    float x, y, z, w;
} Vector4;

enum Attrib {
    ATTRIB_POS = 0,
    ATTRIB_COLOR,
    COUNT_ATTRIBS,
};

static Vector2 seed_positions[SEEDS_COUNT];
static Vector4 seed_colors[SEEDS_COUNT];
static Vector2 seed_velocities[SEEDS_COUNT];
static GLuint vao;
static GLuint vbos[COUNT_ATTRIBS];

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
        float mag = lerpf(100, 200, rand_float());
        seed_velocities[i].x = cosf(angle)*mag;
        seed_velocities[i].y = sinf(angle)*mag;
    }
}

void render_frame(double delta_time)
{
    glClearColor(0.25f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for (size_t i = 0; i < SEEDS_COUNT; ++i) {
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
    glBindBuffer(GL_ARRAY_BUFFER, vbos[ATTRIB_POS]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(seed_positions), seed_positions);

    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, SEEDS_COUNT);
}

void render_video_mode(GLFWwindow *window)
{
    const char *output_dir = "frames";

    if (mkdir(output_dir, 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "ERROR: could not create folder `%s`\n", output_dir);
        exit(1);
    }

    static uint32_t frame_pixels[DEFAULT_SCREEN_HEIGHT][DEFAULT_SCREEN_WIDTH];
    static char file_path[1024];

    size_t fps = 60;
    double delta_time = 1.0/fps;
    double duration = 10.0;
    size_t frames_count = floorf(duration/delta_time);

    for (size_t i = 0; i < frames_count && !glfwWindowShouldClose(window); ++i) {
        render_frame(delta_time);

        glReadPixels(0,
                     0,
                     DEFAULT_SCREEN_WIDTH,
                     DEFAULT_SCREEN_HEIGHT,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     frame_pixels);

        snprintf(file_path, sizeof(file_path), "%s/frame-%03zu.png", output_dir, i);
        if (!stbi_write_png(file_path, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT, 4, frame_pixels, sizeof(uint32_t)*DEFAULT_SCREEN_WIDTH)) {
            fprintf(stderr, "ERROR: could not save file %s\n", file_path);
            exit(1);
        }

        printf("INFO: Rendered %zu/%zu frames\n", i + 1, frames_count);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}

void interactive_mode(GLFWwindow *window)
{
    double prev_time = 0.0;
    double delta_time = 0.0f;

    while (!glfwWindowShouldClose(window)) {
        render_frame(delta_time);

        glfwSwapBuffers(window);
        glfwPollEvents();

        double cur_time = glfwGetTime();
        delta_time = cur_time - prev_time;
        prev_time = cur_time;
    }
}

typedef enum {
    MODE_INTERACTIVE,
    MODE_RENDER_VIDEO,
} Mode;

int main(int argc, char **argv)
{
    Mode mode = MODE_INTERACTIVE;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--video") == 0) {
            mode = MODE_RENDER_VIDEO;
        } else {
            fprintf(stderr, "ERROR: unknown flag `%s`\n", argv[i]);
            exit(1);
        }
    }

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

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(COUNT_ATTRIBS, vbos);

    {
        glGenBuffers(1, &vbos[ATTRIB_POS]);
        glBindBuffer(GL_ARRAY_BUFFER, vbos[ATTRIB_POS]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(seed_positions), seed_positions, GL_DYNAMIC_DRAW);

        glEnableVertexAttribArray(ATTRIB_POS);
        glVertexAttribPointer(ATTRIB_POS,
                              2,
                              GL_FLOAT,
                              GL_FALSE,
                              0,
                              (void*)0);
        glVertexAttribDivisor(ATTRIB_POS, 1);
    }

    {
        glGenBuffers(1, &vbos[ATTRIB_COLOR]);
        glBindBuffer(GL_ARRAY_BUFFER, vbos[ATTRIB_COLOR]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(seed_colors), seed_colors, GL_STATIC_DRAW);

        glEnableVertexAttribArray(ATTRIB_COLOR);
        glVertexAttribPointer(ATTRIB_COLOR,
                              4,
                              GL_FLOAT,
                              GL_FALSE,
                              0,
                              (void*)0);
        glVertexAttribDivisor(ATTRIB_COLOR, 1);
    }

    const char *vertex_file_path = "shaders/quad.vert";
    const char *fragment_file_path = "shaders/color.frag";
    GLuint program;
    if (!load_shader_program(vertex_file_path, fragment_file_path, &program)) {
        exit(1);
    }
    glUseProgram(program);

    // TODO: resize the canvas when the window is resized
    GLint u_resolution = glGetUniformLocation(program, "resolution");
    glUniform2f(u_resolution, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT);

    switch (mode) {
    case MODE_INTERACTIVE:
        interactive_mode(window);
        break;
    case MODE_RENDER_VIDEO:
        render_video_mode(window);
        break;
    default:
        UNREACHABLE("Unexpected execution mode");
    }

    return 0;
}
