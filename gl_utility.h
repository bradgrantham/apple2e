#if !defined(__GLWIDGET_H__)
#define __GLWIDGET_H__

#define GL_SILENCE_DEPRECATION

#if defined(__linux__)
#include <GL/glew.h>
#endif // defined(__linux__)

#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include <vector>
#include <string>

void CheckOpenGL(const char *filename, int line);
bool CheckShaderCompile(GLuint shader, const std::string& shader_name);
bool CheckProgramLink(GLuint program);

struct vertex_attribute_buffer
{
    GLuint buffer;
    GLuint which;
    GLuint count;
    GLenum type;
    GLboolean normalized;
    GLsizei stride;
    void bind() const 
    {
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        CheckOpenGL(__FILE__, __LINE__);
        glVertexAttribPointer(which, count, type, normalized, stride, 0);
        CheckOpenGL(__FILE__, __LINE__);
        glEnableVertexAttribArray(which);
        CheckOpenGL(__FILE__, __LINE__);
    }
};

struct vertex_array : public std::vector<vertex_attribute_buffer>
{
    void bind()
    {
        for(auto attr : *this) {
            attr.bind();
        }
    }
};

/*
 * OpenGL Render Target ; creates a framebuffer that can be used as a
 * rendering target and as a texture color source.
 */
struct render_target
{
    GLuint framebuffer;
    GLuint color;
    GLuint depth;

    render_target(int w, int h);
    ~render_target();

    // Start rendering; Draw()s will draw to this framebuffer
    void start_rendering()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    }

    // Stop rendering; Draw()s will draw to the back buffer
    void stop_rendering()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    // Start reading; Read()s will read from this framebuffer
    void start_reading()
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }

    // Stop reading; Read()s will read from the back buffer
    void stop_reading()
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
    }

    // Use this color as the currently bound texture source
    void use_color()
    {
        glBindTexture(GL_TEXTURE_2D, color);
    }
};

struct opengl_texture
{
    int w;
    int h;
    GLuint t;
    operator GLuint() const { return t; }
    void load(int w, int h, unsigned char *pixels = NULL);
};

opengl_texture initialize_texture(int w, int h, unsigned char *pixels = NULL);

GLuint GenerateProgram(const std::string& shader_name, const std::string& vertex_shader_text, const std::string& fragment_shader_text);

GLuint make_rectangle_array_buffer(float x, float y, float w, float h);

#endif /* __GLWIDGET_H__ */
