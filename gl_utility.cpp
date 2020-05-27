#include <cstdlib>
#include <cstdio>

#define GL_SILENCE_DEPRECATION

#if defined(__linux__)
#include <GL/glew.h>
#endif // defined(__linux__)

#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include "gl_utility.h"

void CheckOpenGL(const char *filename, int line)
{
    int glerr;
    static bool stored_exit_flag = false;
    static bool exit_on_error;

    if(!stored_exit_flag) {
        exit_on_error = getenv("EXIT_ON_OPENGL_ERROR") != NULL;
        stored_exit_flag = true;
    }

    while((glerr = glGetError()) != GL_NO_ERROR) {
        printf("GL Error: %04X at %s:%d\n", glerr, filename, line);
        if(exit_on_error)
            exit(1);
    }
}

// Destroy render target resources
render_target::~render_target()
{
    glDeleteTextures(1, &color);
    glDeleteRenderbuffers(1, &depth);
    glDeleteFramebuffers(1, &framebuffer);
}

// Create render target resources if possible
render_target::render_target(int w, int h)
{
    GLenum status;

    // Create color texture
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    CheckOpenGL(__FILE__, __LINE__);

    // Create depth texture
    glGenTextures(1, &depth);
    glBindTexture(GL_TEXTURE_2D, depth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    CheckOpenGL(__FILE__, __LINE__);

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    CheckOpenGL(__FILE__, __LINE__);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    CheckOpenGL(__FILE__, __LINE__);

    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if(status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "framebuffer status was %04X\n", status);
        throw "Couldn't create OpenGL framebuffer";
    }
    CheckOpenGL(__FILE__, __LINE__);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool gPrintShaderLog = true;

bool CheckShaderCompile(GLuint shader, const std::string& shader_name)
{
    int status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if(status == GL_TRUE)
	return true;

    if(gPrintShaderLog) {
        int length;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        if (length > 0) {
            char log[length];
            glGetShaderInfoLog(shader, length, NULL, log);
            fprintf(stderr, "%s shader error log:\n%s\n", shader_name.c_str(), log);
        }

        fprintf(stderr, "%s compile failure.\n", shader_name.c_str());
        fprintf(stderr, "shader text:\n");
        glGetShaderiv(shader, GL_SHADER_SOURCE_LENGTH, &length);
        char source[length];
        glGetShaderSource(shader, length, NULL, source);
        fprintf(stderr, "%s\n", source);
    }
    return false;
}

bool CheckProgramLink(GLuint program)
{
    int status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if(status == GL_TRUE)
	return true;

    if(gPrintShaderLog) {
        int log_length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);

        if (log_length > 0) {
            char log[log_length];
            glGetProgramInfoLog(program, log_length, NULL, log);
            fprintf(stderr, "program error log: %s\n",log);
        }
    }

    return false;
}

void opengl_texture::load(int w_, int h_, unsigned char *pixels)
{
    w = w_;
    h = h_;
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, GL_NONE);
}

opengl_texture initialize_texture(int w, int h, unsigned char *pixels)
{
    GLuint tex;

    glGenTextures(1, &tex);

    opengl_texture t = {w, h, tex};

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    CheckOpenGL(__FILE__, __LINE__);
    t.load(w, h, pixels);
    CheckOpenGL(__FILE__, __LINE__);
    return t;
}

GLuint GenerateProgram(const std::string& shader_name, const std::string& vertex_shader_text, const std::string& fragment_shader_text)
{
    std::string spec_string;

    spec_string = "#version 140\n";

    // reset line number so that I can view errors with the line number
    // they have in the base shaders.
    spec_string += "#line 0\n";

    std::string vertex_shader_string = spec_string + vertex_shader_text;
    std::string fragment_shader_string = spec_string + fragment_shader_text;

    GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    const char *string = vertex_shader_string.c_str();
    glShaderSource(vertex_shader, 1, &string, NULL);
    glCompileShader(vertex_shader);
    if(!CheckShaderCompile(vertex_shader, shader_name + " vertex shader"))
	return 0;

    GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    string = fragment_shader_string.c_str();
    glShaderSource(fragment_shader, 1, &string, NULL);
    glCompileShader(fragment_shader);
    if(!CheckShaderCompile(fragment_shader, shader_name + " fragment shader"))
	return 0;
    CheckOpenGL(__FILE__, __LINE__);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    CheckOpenGL(__FILE__, __LINE__);

    glLinkProgram(program);
    CheckOpenGL(__FILE__, __LINE__);
    if(!CheckProgramLink(program))
	return 0;

    return program;
}

GLuint make_rectangle_array_buffer(float x, float y, float w, float h)
{
    /* just x, y, also pixel coords */
    GLuint vertices;

    glGenBuffers(1, &vertices);
    glBindBuffer(GL_ARRAY_BUFFER, vertices);
    float coords[4][2] = {
        {x, y},
        {x + w, y},
        {x, y + h},
        {x + w, y + h},
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(coords[0]) * 4, coords, GL_STATIC_DRAW);
    CheckOpenGL(__FILE__, __LINE__);

    return vertices;
}
