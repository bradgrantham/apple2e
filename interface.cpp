#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <deque>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <map>
#include <ao/ao.h>

// implicit centering in widget? Or special centering widget?
// lines (for around toggle and momentary)
// widget which is graphics/text/lores screen
// hbox
// what is window resize / shrink policy?

#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include "interface.h"

using namespace std;

namespace APPLE2Einterface
{

chrono::time_point<chrono::system_clock> start_time;

static GLFWwindow* my_window;
ao_device *aodev;

DisplayMode display_mode = TEXT;
int display_page = 0; // Apple //e page minus 1 (so 0,1 not 1,2)
bool mixed_mode = false;
bool vid80 = false;
bool altchar = false;

bool use_joystick = false;
int joystick_axis0 = 0;
int joystick_axis1 = 1;
int joystick_button0 = 0;
int joystick_button1 = 1;

extern int font_offset;
extern unsigned char font_bytes[96 * 7 * 8];

static int gWindowWidth, gWindowHeight;

bool gPrintShaderLog = true;

// to handle https://github.com/glfw/glfw/issues/161
static double gMotionReported = false;

static double gOldMouseX, gOldMouseY;
static int gButtonPressed = -1;

deque<event> event_queue;

bool force_caps_on = true;
bool draw_using_color = false; // XXX implement!

bool event_waiting()
{
    return event_queue.size() > 0;
}

event dequeue_event()
{
    if(event_waiting()) {
        event e = event_queue.front();
        event_queue.pop_front();
        return e;
    } else
        return {NONE, 0};
}

static void CheckOpenGL(const char *filename, int line)
{
    int glerr;
    bool stored_exit_flag = false;
    bool exit_on_error;

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

struct opengl_texture
{
    int w;
    int h;
    GLuint t;
    operator GLuint() const { return t; }
};

opengl_texture initialize_texture(int w, int h, unsigned char *pixels = NULL)
{
    GLuint tex;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    CheckOpenGL(__FILE__, __LINE__);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    CheckOpenGL(__FILE__, __LINE__);
    return {w, h, tex};
}

opengl_texture initialize_texture_integer(int w, int h, unsigned char *pixels = NULL)
{
    GLuint tex;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    CheckOpenGL(__FILE__, __LINE__);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, w, h, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, pixels);
    CheckOpenGL(__FILE__, __LINE__);
    return {w, h, tex};
}

opengl_texture font_texture;
const int fonttexture_w = 7;
const int fonttexture_h = 8 * 96;

opengl_texture textport_texture[2][2]; // [aux][page]

GLuint text_program;
const int textport_w = 40;
const int textport_h = 24;
GLuint textport_texture_location;
GLuint textport_texture_coord_scale_location;
GLuint textport_blink_location;
GLuint textport_x_offset_location;
GLuint textport_y_offset_location;
GLuint textport_to_screen_location;
GLuint textport_foreground_location;
GLuint textport_background_location;
GLuint textport_font_texture_location;
GLuint textport_font_texture_coord_scale_location;

GLuint text80_program;
GLuint textport80_texture_location;
GLuint textport80_texture_coord_scale_location;
GLuint textport80_aux_texture_location;
GLuint textport80_blink_location;
GLuint textport80_x_offset_location;
GLuint textport80_y_offset_location;
GLuint textport80_to_screen_location;
GLuint textport80_foreground_location;
GLuint textport80_background_location;
GLuint textport80_font_texture_location;
GLuint textport80_font_texture_coord_scale_location;

GLuint lores_program;
GLuint lores_texture_location;
GLuint lores_texture_coord_scale_location;
GLuint lores_x_offset_location;
GLuint lores_y_offset_location;
GLuint lores_to_screen_location;

const int hires_w = 320;  // MSBit is color chooser, Apple ][ weirdness
const int hires_h = 192;
opengl_texture hires_texture[2];

GLuint hires_program;
GLuint hires_texture_location;
GLuint hires_texture_coord_scale_location;
GLuint hires_to_screen_location;
GLuint hires_x_offset_location;
GLuint hires_y_offset_location;

GLuint hirescolor_program;
GLuint hirescolor_texture_location;
GLuint hirescolor_texture_coord_scale_location;
GLuint hirescolor_to_screen_location;
GLuint hirescolor_x_offset_location;
GLuint hirescolor_y_offset_location;

GLuint image_program;
GLuint image_texture_location;
GLuint image_texture_coord_scale_location;
GLuint image_to_screen_location;
GLuint image_x_offset_location;
GLuint image_y_offset_location;

float paddle_values[4] = {0, 0, 0, 0};
bool paddle_buttons[4] = {false, false, false, false};

tuple<float,bool> get_paddle(int num)
{
    if(num < 0 || num > 3)
        make_tuple(-1, false);
    return make_tuple(paddle_values[num], paddle_buttons[num]);
}


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

struct vertex_array : public vector<vertex_attribute_buffer>
{
    void bind()
    {
        for(auto attr : *this) {
            attr.bind();
        }
    }
};

const int raster_coords_attrib = 0;
 
static bool CheckShaderCompile(GLuint shader, const std::string& shader_name)
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

static bool CheckProgramLink(GLuint program)
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

static const char *hires_vertex_shader = "\n\
    uniform mat3 to_screen;\n\
    in vec2 vertex_coords;\n\
    out vec2 raster_coords;\n\
    uniform float x_offset;\n\
    uniform float y_offset;\n\
    \n\
    void main()\n\
    {\n\
        raster_coords = vertex_coords;\n\
        vec3 screen_coords = to_screen * vec3(vertex_coords + vec2(x_offset, y_offset), 1);\n\
        gl_Position = vec4(screen_coords.x, screen_coords.y, .5, 1);\n\
    }\n";

static const char *image_fragment_shader = "\n\
    in vec2 raster_coords;\n\
    uniform vec2 image_coord_scale;\n\
    uniform sampler2D image;\n\
    \n\
    out vec4 color;\n\
    \n\
    void main()\n\
    {\n\
        ivec2 tc = ivec2(raster_coords.x, raster_coords.y);\n\
        float pixel = texture(image, raster_coords * image_coord_scale).x;\n\
        color = vec4(pixel, pixel, pixel, 1);\n\
    }\n";

static const char *hires_fragment_shader = "\n\
    in vec2 raster_coords;\n\
    uniform vec2 hires_texture_coord_scale;\n\
    uniform sampler2D hires_texture;\n\
    \n\
    out vec4 color;\n\
    \n\
    void main()\n\
    {\n\
        int byte = int(raster_coords.x) / 7;\n\
        int bit = int(raster_coords.x) % 7;\n\
        int texturex = byte * 8 + bit;\n\
        ivec2 tc = ivec2(texturex, raster_coords.y);\n\
        float pixel = texture(hires_texture, tc * hires_texture_coord_scale).x;\n\
        color = vec4(pixel, pixel, pixel, 1);\n\
    }\n";

static const char *hirescolor_fragment_shader = "\n\
    in vec2 raster_coords;\n\
    uniform vec2 hires_texture_coord_scale;\n\
    uniform sampler2D hires_texture;\n\
    \n\
    out vec4 color;\n\
    \n\
    vec2 raster_to_texture(int x, int y)\n\
    {\n\
        int byte = x / 7;\n\
        int bit = x % 7;\n\
        int texturex = byte * 8 + bit;\n\
        return vec2(texturex, y) * hires_texture_coord_scale; \n\
    }\n\
    void main()\n\
    {\n\
        int x = int(raster_coords.x); \n\
        int y = int(raster_coords.y); \n\
 \n\
        uint left = (x < 1) ? 0u : uint(255 * texture(hires_texture, raster_to_texture(x - 1, y)).x);\n\
        uint pixel = uint(255 * texture(hires_texture, raster_to_texture(x, y)).x);\n\
        uint right = (x > 278) ? 0u : uint(255 * texture(hires_texture, raster_to_texture(x + 1, y)).x);\n\
 \n\
        if((pixel == 255u) && ((left == 255u) || (right == 255u))) { \n\
            /* Okay, first of all, if this pixel's on and its left or right are on, it's white. */ \n\
            color = vec4(1.0, 1.0, 1.0, 1.0);\n\
        } else if((pixel == 0u) && (left == 0u) && (right == 0u)) { \n\
            /* If none are on, it's black */ \n\
            color = vec4(0.0, 0.0, 0.0, 1.0);\n\
        } else { \n\
            uint even = (x % 2 == 1) ? left : pixel; \n\
            uint odd = (x % 2 == 1) ? pixel : right; \n\
            uint palette = uint(texture(hires_texture, vec2((x / 7) * 8 + 7, raster_coords.y) * hires_texture_coord_scale).x); \n\
 \n\
            if(palette == 0u) { \n\
                if((even == 0u) && (odd == 255u)) { \n\
                    color = vec4(20.0/255.0, 245.0/255.0, 60.0/255.0, 1.0);\n\
                    /* green 20 245  60 */ \n\
                } else if((even == 255u) && (odd == 0u)) { \n\
                    /* purple 255  68 253 */ \n\
                    color = vec4(255.0/255.0, 68.0/255.0, 253.0/255.0, 1.0);\n\
                } else if((even == 0u) && (odd == 0u)) { \n\
                    color = vec4(0, 0, 0, 1);\n\
                } /* handled 1,1 above */ \n\
            } else { \n\
                if((even == 0u) && (odd == 255u)) { \n\
                    /* orange 255 106  60 */ \n\
                    color = vec4(255.0/255.0, 106.0/255.0, 60.0/255.0, 1.0);\n\
                } else if((even == 255u) && (odd == 0u)) { \n\
                    /* blue 20 207 253 */ \n\
                    color = vec4(20.0/255.0, 207.0/255.0, 253.0/255.0, 1.0);\n\
                } else if((even == 0u) && (odd == 0u)) { \n\
                    color = vec4(0, 0, 0, 1);\n\
                } /* handled 1,1 above */ \n\
            } \n\
        } \n\
    }\n";

static const char *text_vertex_shader = "\n\
    uniform mat3 to_screen;\n\
    in vec2 vertex_coords;\n\
    uniform float x_offset;\n\
    uniform float y_offset;\n\
    out vec2 raster_coords;\n\
    \n\
    void main()\n\
    {\n\
        raster_coords = vertex_coords;\n\
        vec3 screen_coords = to_screen * vec3(vertex_coords + vec2(x_offset, y_offset), 1);\n\
        gl_Position = vec4(screen_coords.x, screen_coords.y, .5, 1);\n\
    }\n";

// 0-31 is inverse 32-63
// 32-63 is inverse 0-31
// 64-95 is blink 32-63
// 96-127 is blink 0-31
// 128-159 is normal 32-63
// 160-191 is normal 0-31
// 192-223 is normal 32-63
// 224-255 is normal 64-95

static const char *text_fragment_shader = "\n\
    in vec2 raster_coords;\n\
    uniform int blink;\n\
    uniform vec4 foreground;\n\
    uniform vec4 background;\n\
    uniform vec2 font_texture_coord_scale;\n\
    uniform usampler2D font_texture;\n\
    uniform vec2 textport_texture_coord_scale;\n\
    uniform usampler2D textport_texture;\n\
    \n\
    out vec4 color;\n\
    \n\
    void main()\n\
    {\n\
        uint character;\n\
        character = texture(textport_texture, uvec2(uint(raster_coords.x) / 7u, uint(raster_coords.y) / 8u) * textport_texture_coord_scale).x; \n\
        bool inverse = false;\n\
        if(character >= 0u && character <= 31u) {\n\
            character = character - 0u + 32u;\n\
            inverse = true;\n\
        } else if(character >= 32u && character <= 63u) {\n\
            character = character - 32u + 0u;\n\
            inverse = true;\n\
        } else if(character >= 64u && character <= 95u) {\n\
            character = character - 64u + 32u; // XXX BLINK \n\
            inverse = blink == 1;\n\
        } else if(character >= 96u && character <= 127u){\n\
            character = character - 96u + 0u; // XXX BLINK \n\
            inverse = blink == 1;\n\
        } else if(character >= 128u && character <= 159u)\n\
            character = character - 128u + 32u;\n\
        else if(character >= 160u && character <= 191u)\n\
            character = character - 160u + 0u;\n\
        else if(character >= 192u && character <= 223u)\n\
            character = character - 192u + 32u;\n\
        else if(character >= 224u && character <= 255u)\n\
            character = character - 224u + 64u;\n\
        else \n\
            character = 33u;\n\
        uvec2 inglyph = uvec2(uint(raster_coords.x) % 7u, uint(raster_coords.y) % 8u);\n\
        uvec2 infont = inglyph + uvec2(0, character * 8u);\n\
        uint pixel = texture(font_texture, infont * font_texture_coord_scale).x;\n\
        float value;\n\
        if(inverse)\n\
            color = mix(background, foreground, 1.0 - pixel / 255.0);\n\
        else\n\
            color = mix(background, foreground, pixel / 255.0);\n\
    }\n";

static const char *text80_fragment_shader = "\n\
    in vec2 raster_coords;\n\
    uniform int blink;\n\
    uniform vec4 foreground;\n\
    uniform vec4 background;\n\
    uniform vec2 font_texture_coord_scale;\n\
    uniform usampler2D font_texture;\n\
    uniform vec2 textport_texture_coord_scale;\n\
    uniform usampler2D textport_texture;\n\
    uniform usampler2D textport_aux_texture;\n\
    \n\
    out vec4 color;\n\
    \n\
    void main()\n\
    {\n\
        uint character;\n\
        uint x = uint(raster_coords.x * 2) / 7u; \n\
        if(x % 2u == 1u) \n\
            character = texture(textport_texture, uvec2((x - 1u) / 2u, uint(raster_coords.y) / 8u) * textport_texture_coord_scale).x; \n\
        else \n\
            character = texture(textport_aux_texture, uvec2(x / 2u, uint(raster_coords.y) / 8u) * textport_texture_coord_scale).x; \n\
        bool inverse = false;\n\
        if(character >= 0u && character <= 31u) {\n\
            character = character - 0u + 32u;\n\
            inverse = true;\n\
        } else if(character >= 32u && character <= 63u) {\n\
            character = character - 32u + 0u;\n\
            inverse = true;\n\
        } else if(character >= 64u && character <= 95u) {\n\
            character = character - 64u + 32u; // XXX BLINK \n\
            inverse = blink == 1;\n\
        } else if(character >= 96u && character <= 127u){\n\
            character = character - 96u + 0u; // XXX BLINK \n\
            inverse = blink == 1;\n\
        } else if(character >= 128u && character <= 159u)\n\
            character = character - 128u + 32u;\n\
        else if(character >= 160u && character <= 191u)\n\
            character = character - 160u + 0u;\n\
        else if(character >= 192u && character <= 223u)\n\
            character = character - 192u + 32u;\n\
        else if(character >= 224u && character <= 255u)\n\
            character = character - 224u + 64u;\n\
        else \n\
            character = 33u;\n\
        uvec2 inglyph = uvec2(uint(raster_coords.x * 2) % 7u, uint(raster_coords.y) % 8u);\n\
        uvec2 infont = inglyph + uvec2(0, character * 8u);\n\
        uint pixel = texture(font_texture, infont * font_texture_coord_scale).x;\n\
        float value;\n\
        if(inverse)\n\
            color = mix(background, foreground, 1.0 - pixel / 255.0);\n\
        else\n\
            color = mix(background, foreground, pixel / 255.0);\n\
    }\n";

static const char *lores_fragment_shader = "\n\
    in vec2 raster_coords;\n\
    uniform vec2 lores_texture_coord_scale;\n\
    uniform usampler2D lores_texture;\n\
    \n\
    out vec4 color;\n\
    \n\
    void main()\n\
    {\n\
        uint byte;\n\
        byte = texture(lores_texture, uvec2(uint(raster_coords.x) / 7u, uint(raster_coords.y) / 8u) * lores_texture_coord_scale).x; \n\
        uint inglyph_y = uint(raster_coords.y) % 8u;\n\
        uint lorespixel;\n\
        if(inglyph_y < 4u)\n\
            lorespixel = byte % 16u;\n\
        else\n\
            lorespixel = byte / 16u;\n\
        switch(lorespixel) {\n\
            case 0:\n\
                color = vec4(0, 0, 0, 1);\n\
                break;\n\
            case 1:\n\
                color = vec4(227.0/255.0, 30.0/255.0, 96.0/255.0, 1);\n\
                break;\n\
            case 2:\n\
                color = vec4(96.0/255.0, 78.0/255.0, 189.0/255.0, 1);\n\
                break;\n\
            case 3:\n\
                color = vec4(255.0/255.0, 68.0/255.0, 253.0/255.0, 1);\n\
                break;\n\
            case 4:\n\
                color = vec4(9.0/255.0, 163.0/255.0, 96.0/255.0, 1);\n\
                break;\n\
            case 5:\n\
                color = vec4(156.0/255.0, 156.0/255.0, 156.0/255.0, 1);\n\
                break;\n\
            case 6:\n\
                color = vec4(20.0/255.0, 207.0/255.0, 253.0/255.0, 1);\n\
                break;\n\
            case 7:\n\
                color = vec4(208.0/255.0, 195.0/255.0, 255.0/255.0, 1);\n\
                break;\n\
            case 8:\n\
                color = vec4(96.0/255.0, 114.0/255.0, 3.0/255.0, 1);\n\
                break;\n\
            case 9:\n\
                color = vec4(255.0/255.0, 106.0/255.0, 60.0/255.0, 1);\n\
                break;\n\
            case 10:\n\
                color = vec4(156.0/255.0, 156.0/255.0, 156.0/255.0, 1);\n\
                break;\n\
            case 11:\n\
                color = vec4(255.0/255.0, 160.0/255.0, 208.0/255.0, 1);\n\
                break;\n\
            case 12:\n\
                color = vec4(20.0/255.0, 245.0/255.0, 60.0/255.0, 1);\n\
                break;\n\
            case 13:\n\
                color = vec4(208.0/255.0, 221.0/255.0, 141.0/255.0, 1);\n\
                break;\n\
            case 14:\n\
                color = vec4(114.0/255.0, 255.0/255.0, 208.0/255.0, 1);\n\
                break;\n\
            case 15:\n\
                color = vec4(255.0/255.0, 255.0/255.0, 255.0/255.0, 1);\n\
                break;\n\
        }\n\
    }\n";

static GLuint GenerateProgram(const string& shader_name, const string& vertex_shader_text, const string& fragment_shader_text)
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

    // XXX Really need to do this generically
    glBindAttribLocation(program, raster_coords_attrib, "vertex_coords");
    CheckOpenGL(__FILE__, __LINE__);

    glLinkProgram(program);
    CheckOpenGL(__FILE__, __LINE__);
    if(!CheckProgramLink(program))
	return 0;

    return program;
}

vertex_array upper_screen_area;
vertex_array lower_screen_area;

vertex_array make_rectangle_vertex_array(float x, float y, float w, float h)
{
    vertex_array array;

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

    array.push_back({vertices, raster_coords_attrib, 2, GL_FLOAT, GL_FALSE, 0});

    return array;
}

void initialize_screen_areas()
{
    upper_screen_area = make_rectangle_vertex_array(0, 0, 280, 160);
    lower_screen_area = make_rectangle_vertex_array(0, 160, 280, 32);
}

void set_image_shader(float to_screen[9], const opengl_texture& texture, float x, float y)
{
    glUseProgram(image_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform2f(image_texture_coord_scale_location, 1.0 / texture.w, 1.0 / texture.h);
    glUniform1i(image_texture_location, 0);
    glUniformMatrix3fv(image_to_screen_location, 1, GL_FALSE, to_screen);
    glUniform1f(image_x_offset_location, x);
    glUniform1f(image_y_offset_location, y);
}

void set_hires_shader(float to_screen[9], const opengl_texture& texture, bool color, float x, float y)
{
    if(color) {
        glUseProgram(hirescolor_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform2f(hirescolor_texture_coord_scale_location, 1.0 / texture.w, 1.0 / texture.h);
        glUniform1i(hirescolor_texture_location, 0);
        glUniformMatrix3fv(hirescolor_to_screen_location, 1, GL_FALSE, to_screen);
        glUniform1f(hirescolor_x_offset_location, x);
        glUniform1f(hirescolor_y_offset_location, y);
        CheckOpenGL(__FILE__, __LINE__);

    } else {

        glUseProgram(hires_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform2f(hires_texture_coord_scale_location, 1.0 / texture.w, 1.0 / texture.h);
        glUniform1i(hires_texture_location, 0);
        glUniformMatrix3fv(hires_to_screen_location, 1, GL_FALSE, to_screen);
        glUniform1f(hires_x_offset_location, x);
        glUniform1f(hires_y_offset_location, y);
        CheckOpenGL(__FILE__, __LINE__);
    }
}

void set_textport_shader(float to_screen[9], const opengl_texture& textport, int blink, float x, float y, float fg[4], float bg[4])
{
    glUseProgram(text_program);

    glActiveTexture(GL_TEXTURE0);
    CheckOpenGL(__FILE__, __LINE__);
    glBindTexture(GL_TEXTURE_2D, textport);
    CheckOpenGL(__FILE__, __LINE__);
    glUniform1i(textport_texture_location, 0);
    CheckOpenGL(__FILE__, __LINE__);
    glUniform2f(textport_texture_coord_scale_location, 1.0 / textport.w, 1.0 / textport.h);
    CheckOpenGL(__FILE__, __LINE__);
    glActiveTexture(GL_TEXTURE1);
    CheckOpenGL(__FILE__, __LINE__);
    glBindTexture(GL_TEXTURE_2D, font_texture);
    CheckOpenGL(__FILE__, __LINE__);
    glUniform1i(textport_font_texture_location, 1);
    CheckOpenGL(__FILE__, __LINE__);
    glUniform2f(textport_font_texture_coord_scale_location, 1.0 / fonttexture_w, 1.0 / fonttexture_h);
    CheckOpenGL(__FILE__, __LINE__);
    glUniform1i(textport_blink_location, blink);
    glUniform1f(textport_x_offset_location, x);
    glUniform1f(textport_y_offset_location, y);
    glUniform4fv(textport_background_location, 1, bg);
    glUniform4fv(textport_foreground_location, 1, fg);
    glUniformMatrix3fv(textport_to_screen_location, 1, GL_FALSE, to_screen);
    CheckOpenGL(__FILE__, __LINE__);
}

void set_textport80_shader(float to_screen[9], const opengl_texture& textport80, GLuint textport80_aux_texture, int blink, float x, float y, float fg[4], float bg[4])
{
    glUseProgram(text80_program);
    CheckOpenGL(__FILE__, __LINE__);

    int tex = 0;
    glActiveTexture(GL_TEXTURE0 + tex);
    glBindTexture(GL_TEXTURE_2D, textport80);
    glUniform1i(textport80_texture_location, tex);
    glUniform2f(textport80_texture_coord_scale_location, 1.0 / textport_w, 1.0 / textport_h);
    tex += 1;
    CheckOpenGL(__FILE__, __LINE__);
    glActiveTexture(GL_TEXTURE0 + tex);
    glBindTexture(GL_TEXTURE_2D, textport80_aux_texture);
    glUniform1i(textport80_aux_texture_location, tex);
    tex += 1;
    CheckOpenGL(__FILE__, __LINE__);
    glActiveTexture(GL_TEXTURE0 + tex);
    glBindTexture(GL_TEXTURE_2D, font_texture);
    glUniform1i(textport80_font_texture_location, tex);
    glUniform2f(textport80_font_texture_coord_scale_location, 1.0 / fonttexture_w, 1.0 / fonttexture_h);
    tex += 1;
    glUniform1i(textport80_blink_location, blink);
    glUniform1f(textport80_x_offset_location, x);
    glUniform1f(textport80_y_offset_location, y);
    glUniform4fv(textport80_background_location, 1, bg);
    glUniform4fv(textport80_foreground_location, 1, fg);
    glUniformMatrix3fv(textport80_to_screen_location, 1, GL_FALSE, to_screen);
    CheckOpenGL(__FILE__, __LINE__);
}

void set_shader(float to_screen[9], DisplayMode display_mode, bool mixed_mode, bool vid80, int blink, float x, float y)
{
    if(mixed_mode || (display_mode == TEXT)) {

        float bg[4] = {0, 0, 0, 1};
        float fg[4] = {1, 1, 1, 1};
        if(vid80) 
            set_textport80_shader(to_screen, textport_texture[0][display_page], textport_texture[1][display_page], blink, x, y, fg, bg);
        else
            set_textport_shader(to_screen, textport_texture[0][display_page], blink, x, y, fg, bg);

    } else if(display_mode == LORES) {

        glUseProgram(lores_program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textport_texture[0][display_page]);
        glUniform1i(lores_texture_location, 0);
        glUniform2f(lores_texture_coord_scale_location, 1.0 / (textport_w), 1.0 / (textport_h));
        glUniformMatrix3fv(lores_to_screen_location, 1, GL_FALSE, to_screen);
        glUniform1f(lores_x_offset_location, x);
        glUniform1f(lores_y_offset_location, y);

    } else if(display_mode == HIRES) {

        set_hires_shader(to_screen, hires_texture[display_page], draw_using_color, x, y);

    }
}

struct widget
{
    virtual tuple<float, float> get_min_dimensions() const = 0;
    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h) {};
    virtual bool click(double now, float x, float y) { return false; };
    virtual void hover(double now, float x, float y) {};
    virtual void drag(double now, float x, float y) {};
    virtual void release(double now, float x, float y) {};
    virtual bool drop(double now, float x, float y, int count, const char** paths) { return false; };
};

struct switcher
{
    float w, h;
    int which;
    vector<widget*> children;
    switcher(vector<widget*> children_) :
        w(0),
        h(0),
        which(0),
        children(children_)
    {
        for(auto it : children) {
            float cw, ch;
            tie(cw, ch) = it->get_min_dimensions();
            w = max(w, cw);
            h = max(h, ch);
        }
    }
    virtual tuple<float, float> get_min_dimensions() const 
    {
        return make_tuple(w, h);
    }
    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h)
    {
        children[which]->draw(now, to_screen, x, y, w, h);
    }
    virtual bool click(double now, float x, float y)
    {
        return children[which]->click(now, x, y);
    }
    virtual void hover(double now, float x, float y)
    {
        children[which]->hover(now, x, y);
    }
    virtual void drag(double now, float x, float y)
    {
        children[which]->drag(now, x, y);
    }
    virtual void release(double now, float x, float y)
    {
        children[which]->release(now, x, y);
    }
    virtual bool drop(double now, float x, float y, int count, const char** paths)
    {
        return children[which]->drop(now, x, y, count, paths);
    }
};

struct spacer : public widget
{
    float w, h;
    spacer(float w_, float h_) :
        w(w_),
        h(h_)
    {}
    virtual tuple<float, float> get_min_dimensions() const 
    {
        return make_tuple(w, h);
    }
};

struct placed_widget
{
    widget *widg;
    float x, y, w, h;
};

struct padding : public widget
{
    widget* child;
    float w, h;
    float left_pad, right_pad, top_pad, bottom_pad;
    float cw, ch;

    padding(float left_pad_, float right_pad_, float top_pad_, float bottom_pad_, widget* child_) :
        child(child_),
        left_pad(left_pad_),
        right_pad(right_pad_),
        top_pad(top_pad_),
        bottom_pad(bottom_pad_)
    {
        tie(cw, ch) = child->get_min_dimensions();
        w = cw + left_pad_ + right_pad_;
        h = ch + top_pad_ + bottom_pad_;
    }

    virtual tuple<float, float> get_min_dimensions() const
    {
        return make_tuple(w, h);
    }
    virtual void draw(double now, float to_screen[9], float x, float y, float w_, float h_)
    {
        child->draw(now, to_screen, x + left_pad, y + top_pad, w_ - left_pad - right_pad, h_ - top_pad - bottom_pad);
    }
    virtual bool drop(double now, float x, float y, int count, const char **paths)
    {
        return child->drop(now, x + left_pad, y + top_pad, count, paths);
    }
    virtual bool click(double now, float x, float y)
    {
        return child->click(now, x + left_pad, y + top_pad);
    }
    virtual void hover(double now, float x, float y)
    {
        child->hover(now, x + left_pad, y + top_pad);
    }
    virtual void drag(double now, float x, float y)
    {
        child->drag(now, x + left_pad, y + top_pad);
    }
    virtual void release(double now, float x, float y)
    {
        child->release(now, x + left_pad, y + top_pad);
    }
};

struct centering : public widget
{
    float w, h;
    float cw, ch;
    widget* child;

    centering(widget* child_) : child(child_)
    {
        tie(cw, ch) = child->get_min_dimensions();
    }

    virtual tuple<float, float> get_min_dimensions() const
    {
        return make_tuple(cw, ch);
    }
    virtual void draw(double now, float to_screen[9], float x, float y, float w_, float h_)
    {
        w = w_;
        h = h_;
        child->draw(now, to_screen, x + (w - cw) / 2, y + (h - ch) / 2, cw, ch);
    }
    virtual bool drop(double now, float x, float y, int count, const char **paths)
    {
        return child->drop(now, x - (w - cw) / 2, y - (h - ch) / 2, count, paths);
    }
    virtual bool click(double now, float x, float y)
    {
        return child->click(now, x - (w - cw) / 2, y - (h - ch) / 2); // XXX should limit to cw,ch too
    }
    virtual void hover(double now, float x, float y)
    {
        child->hover(now, x - (w - cw) / 2, y - (h - ch) / 2);
    }
    virtual void drag(double now, float x, float y)
    {
        child->drag(now, x - (w - cw) / 2, y - (h - ch) / 2);
    }
    virtual void release(double now, float x, float y)
    {
        child->release(now, x - (w - cw) / 2, y - (h - ch) / 2);
    }
};

struct widgetbox : public widget
{
    enum Direction {VERTICAL, HORIZONTAL} dir;
    float w, h;
    vector<placed_widget> children;
    placed_widget focus;

    widgetbox(Direction dir_, vector<widget*> children_) :
        dir(dir_),
        w(0),
        h(0),
        focus({nullptr, 0, 0, 0, 0})
    {
        for(auto it : children_) {
            widget *child = it;
            float cw, ch;
            tie(cw, ch) = child->get_min_dimensions();
            if(dir == HORIZONTAL) {
                w += cw;
                h = std::max(h, ch);
            } else {
                w = std::max(w, cw);
                h += ch;
            }
        }
        float x = 0;
        float y = 0;
        for(auto it : children_) {
            widget *child = it;
            float cw, ch;
            tie(cw, ch) = child->get_min_dimensions();
            if(dir == HORIZONTAL) {
                children.push_back({child, x, y, cw, h});
                x += cw;
            } else {
                children.push_back({child, x, y, w, ch});
                y += ch;
            }
        }
    }
    virtual tuple<float, float> get_min_dimensions() const
    {
        return make_tuple(w, h);
    }
    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h)
    {
        for(auto child : children) {
            child.widg->draw(now, to_screen, x + child.x, y + child.y, child.w, child.h);
        }
    }
    virtual bool drop(double now, float x, float y, int count, const char **paths)
    {
        for(auto child : children) {
            if(child.widg->drop(now, x - child.x, y - child.y, count, paths)) {
                return true;
            }
        }
        return false;
    }
    virtual bool click(double now, float x, float y)
    {
        for(auto child : children) {
            if(child.widg->click(now, x - child.x, y - child.y)) {
                focus = child;
                return true;
            }
        }
        return false;
    }
    virtual void hover(double now, float x, float y)
    {
        for(auto child : children) {
            if(x >= child.x && x < child.x + child.w && y >= child.y && y < child.y + child.h)
                child.widg->hover(now, x - child.x, y - child.y);
        }
    }
    virtual void drag(double now, float x, float y)
    {
        focus.widg->click(now, x - focus.x, y - focus.y);
    }
    virtual void release(double now, float x, float y)
    {
        focus.widg->release(now, x - focus.x, y - focus.y);
        focus = {nullptr, 0, 0};
    }
};

void set(float v[4], float x, float y, float z, float w)
{
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = w;
}

struct apple2screen : public widget
{
    float w, h;
    apple2screen() { }

    virtual tuple<float, float> get_min_dimensions() const
    {
        return make_tuple(280, 192);
    }

    virtual void draw(double now, float to_screen[9], float x, float y, float w_, float h_)
    {
        w = w_;
        h = h_;
        long long elapsed_millis = now * 1000;
        set_shader(to_screen, display_mode, false, vid80, (elapsed_millis / 300) % 2, x, y);
        CheckOpenGL(__FILE__, __LINE__);

        upper_screen_area.bind();
        CheckOpenGL(__FILE__, __LINE__);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CheckOpenGL(__FILE__, __LINE__);

        set_shader(to_screen, display_mode, mixed_mode, vid80, (elapsed_millis / 300) % 2, x, y);

        lower_screen_area.bind();
        CheckOpenGL(__FILE__, __LINE__);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CheckOpenGL(__FILE__, __LINE__);
    }

    virtual bool click(double now, float x, float y)
    {
        float w, h;
        tie(w, h) = get_min_dimensions();
        if(x >= 0 && y >= 0 & x < w && y < h) {
            if(!use_joystick)
                paddle_buttons[0] = true;
            return true;
            // XXX paddle button 1
        }
        return false;
    }

    virtual void drag(double now, float x, float y)
    {
        if(!use_joystick) {
            paddle_values[0] = max(0.0f, min(1.0f, x / w));
            paddle_values[1] = max(0.0f, min(1.0f, y / h));
        }
    }

    virtual void hover(double now, float x, float y)
    {
        if(!use_joystick) {
            paddle_values[0] = max(0.0f, min(1.0f, x / w));
            paddle_values[1] = max(0.0f, min(1.0f, y / h));
        }
    }

    virtual void release(double now, float x, float y)
    {
        if(!use_joystick) {
            paddle_buttons[0] = false;
        }
    }

    virtual bool drop(double now, float x, float y, int count, const char** paths)
    {
        // insert
        float w, h;
        tie(w, h) = get_min_dimensions();
        if(x >= 0 && y >= 0 & x < w && y < h) {
            FILE *fp = fopen(paths[0], "r");
            fseek(fp, 0, SEEK_END);
            long length = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            char *text = (char *)malloc(length);
            fread(text, 1, length, fp);
            event_queue.push_back({PASTE, 0, text});
            fclose(fp);
            return true;
        }
        return false;
    }
};

struct image_widget : public widget
{
    opengl_texture image;
    vertex_array rectangle;
    int w, h;

    image_widget(int w_, int h_, unsigned char *buffer) :
        w(w_),
        h(h_)
    {
        image = initialize_texture(w, h, buffer);
        rectangle = make_rectangle_vertex_array(0, 0, w, h);
    }

    virtual tuple<float, float> get_min_dimensions() const
    {
        return make_tuple(w, h);
    }

    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h)
    {
        set_image_shader(to_screen, image, x, y);

        rectangle.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CheckOpenGL(__FILE__, __LINE__);
    }
};

struct text_widget : public widget
{
    opengl_texture string_texture;
    vertex_array rectangle;
    string content;
    float fg[4];
    float bg[4];

    text_widget(const string& content_) :
        content(content_)
    {
        set(fg, 1, 1, 1, 0);
        set(bg, 0, 0, 0, 0);

        // construct string texture
        auto_ptr<unsigned char> bytes(new unsigned char[content.size() + 1]);
        int i = 0;
        for(auto c : content) {
            if(c >= ' ' && c <= '?')
                bytes.get()[i] = c - ' ' + 160;
            else if(c >= '@' && c <= '_')
                bytes.get()[i] = c - '@' + 128;
            else if(c >= '`' && c <= '~')
                bytes.get()[i] = c - '`' + 224;
            else
                bytes.get()[i] = 255;
            i++;
        }
        string_texture = initialize_texture_integer(i, 1, bytes.get());
        rectangle = make_rectangle_vertex_array(0, 0, i * 7, 8);
    }

    virtual tuple<float, float> get_min_dimensions() const
    {
        return make_tuple(content.size() * 7, 8);
    }

    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h)
    {
        set_textport_shader(to_screen, string_texture, 0, x, y, fg, bg);

        rectangle.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CheckOpenGL(__FILE__, __LINE__);
    }
};

struct momentary : public text_widget
{
    bool on;
    std::function<void()> action;

    momentary(const string& content_, std::function<void()> action_) :
        text_widget(content_),
        on(false),
        action(action_)
    {
        set(bg, 0, 0, 0, 1);
        set(fg, 1, 1, 1, 1);
    }

    virtual tuple<float, float> get_min_dimensions() const
    {
        float w, h;
        tie(w, h) = text_widget::get_min_dimensions();
        return make_tuple(w + 3 * 2, h + 3 * 2);
    }

    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h)
    {
        // draw lines 2 pixels around
        // draw lines 1 pixels around
        // blank area 0 pixels around

        text_widget::draw(now, to_screen, x + 3, y + 3, w - 6, h - 6);
    }

    virtual bool click(double now, float x, float y)
    {
        float w, h;
        tie(w, h) = get_min_dimensions();
        if(x >= 0 && y >= 0 & x < w && y < h) {
            on = true;
            set(fg, 0, 0, 0, 1);
            set(bg, 1, 1, 1, 1);
            return true;
        }
        return false;
    }

    virtual void drag(double now, float x, float y)
    {
        float w, h;
        tie(w, h) = get_min_dimensions();
        on = (x >= 0 && y >= 0 & x < w && y < h);
        if(on) {
            set(fg, 0, 0, 0, 1);
            set(bg, 1, 1, 1, 1);
        } else {
            set(bg, 0, 0, 0, 1);
            set(fg, 1, 1, 1, 1);
        }
    }

    virtual void release(double now, float x, float y)
    {
        action();
        on = false;
        set(bg, 0, 0, 0, 1);
        set(fg, 1, 1, 1, 1);
    }
};

struct toggle : public text_widget
{
    bool on;
    std::function<void()> action_on;
    std::function<void()> action_off;

    toggle(const string& content_, bool initial_state, std::function<void()> action_on_, std::function<void()> action_off_) :
        text_widget(content_),
        on(initial_state),
        action_on(action_on_),
        action_off(action_off_)
    {
        if(initial_state) {
            set(fg, 0, 0, 0, 1);
            set(bg, 1, 1, 1, 1);
        } else {
            set(fg, 1, 1, 1, 1);
            set(bg, 0, 0, 0, 1);
        }
    }

    virtual tuple<float, float> get_min_dimensions() const
    {
        float w, h;
        tie(w, h) = text_widget::get_min_dimensions();
        return make_tuple(w + 3 * 2, h + 3 * 2);
    }

    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h)
    {
        // draw lines 2 pixels around
        // draw lines 1 pixels around
        // blank area 0 pixels around

        text_widget::draw(now, to_screen, x + 3, y + 3, w - 6, h - 6);
    }

    virtual bool click(double now, float x, float y)
    {
        float w, h;
        tie(w, h) = get_min_dimensions();
        if(x >= 0 && y >= 0 & x < w && y < h) {
            if(on) {
                set(fg, 0, 0, 0, 1);
                set(bg, 1, 1, 1, 1);
            } else {
                set(fg, 1, 1, 1, 1);
                set(bg, 0, 0, 0, 1);
            }
            return true;
        }
        return false;
    }

    virtual void drag(double now, float x, float y)
    {
        float w, h;
        tie(w, h) = get_min_dimensions();
        if(x >= 0 && y >= 0 & x < w && y < h) {
            if(on) {
                set(fg, 0, 0, 0, 1);
                set(bg, 1, 1, 1, 1);
            } else {
                set(fg, 1, 1, 1, 1);
                set(bg, 0, 0, 0, 1);
            }
        } else {
            if(on) {
                set(bg, 0, 0, 0, 1);
                set(fg, 1, 1, 1, 1);
            } else {
                set(bg, 1, 1, 1, 1);
                set(fg, 0, 0, 0, 1);
            }
        }
    }

    virtual void release(double now, float x, float y)
    {
        if(on) {
            action_off();
            set(bg, 0, 0, 0, 1);
            set(fg, 1, 1, 1, 1);
        } else {
            action_on();
            set(bg, 1, 1, 1, 1);
            set(fg, 0, 0, 0, 1);
        }
        on = !on;
    }
};

widget *ui;
toggle *caps_toggle;

void initialize_gl(void)
{
    GLuint va;
    glGenVertexArrays(1, &va);
    glBindVertexArray(va);

    glClearColor(0, 0, 0, 1);
    CheckOpenGL(__FILE__, __LINE__);

    font_texture = initialize_texture_integer(fonttexture_w, fonttexture_h, font_bytes);
    textport_texture[0][0] = initialize_texture_integer(textport_w, textport_h);
    textport_texture[0][1] = initialize_texture_integer(textport_w, textport_h);
    textport_texture[1][0] = initialize_texture_integer(textport_w, textport_h);
    textport_texture[1][1] = initialize_texture_integer(textport_w, textport_h);
    hires_texture[0] = initialize_texture(hires_w, hires_h);
    hires_texture[1] = initialize_texture(hires_w, hires_h);
    CheckOpenGL(__FILE__, __LINE__);

    image_program = GenerateProgram("image", hires_vertex_shader, image_fragment_shader);
    image_texture_location = glGetUniformLocation(image_program, "image");
    image_texture_coord_scale_location = glGetUniformLocation(image_program, "image_coord_scale");
    image_to_screen_location = glGetUniformLocation(image_program, "to_screen");
    image_x_offset_location = glGetUniformLocation(image_program, "x_offset");
    image_y_offset_location = glGetUniformLocation(image_program, "y_offset");

    hires_program = GenerateProgram("hires", hires_vertex_shader, hires_fragment_shader);
    hires_texture_location = glGetUniformLocation(hires_program, "hires_texture");
    hires_texture_coord_scale_location = glGetUniformLocation(hires_program, "hires_texture_coord_scale");
    hires_to_screen_location = glGetUniformLocation(hires_program, "to_screen");
    hires_x_offset_location = glGetUniformLocation(hires_program, "x_offset");
    hires_y_offset_location = glGetUniformLocation(hires_program, "y_offset");

    hirescolor_program = GenerateProgram("hirescolor", hires_vertex_shader, hirescolor_fragment_shader);
    hirescolor_texture_location = glGetUniformLocation(hirescolor_program, "hires_texture");
    hirescolor_texture_coord_scale_location = glGetUniformLocation(hirescolor_program, "hires_texture_coord_scale");
    hirescolor_to_screen_location = glGetUniformLocation(hirescolor_program, "to_screen");
    hirescolor_x_offset_location = glGetUniformLocation(hirescolor_program, "x_offset");
    hirescolor_y_offset_location = glGetUniformLocation(hirescolor_program, "y_offset");

    text_program = GenerateProgram("textport", text_vertex_shader, text_fragment_shader);
    textport_texture_location = glGetUniformLocation(text_program, "textport_texture");
    textport_texture_coord_scale_location = glGetUniformLocation(text_program, "textport_texture_coord_scale");
    textport_font_texture_location = glGetUniformLocation(text_program, "font_texture");
    textport_font_texture_coord_scale_location = glGetUniformLocation(text_program, "font_texture_coord_scale");
    textport_blink_location = glGetUniformLocation(text_program, "blink");
    textport_x_offset_location = glGetUniformLocation(text_program, "x_offset");
    textport_y_offset_location = glGetUniformLocation(text_program, "y_offset");
    textport_to_screen_location = glGetUniformLocation(text_program, "to_screen");
    textport_foreground_location = glGetUniformLocation(text_program, "foreground");
    textport_background_location = glGetUniformLocation(text_program, "background");
    CheckOpenGL(__FILE__, __LINE__);

    text80_program = GenerateProgram("textport80", text_vertex_shader, text80_fragment_shader);
    textport80_texture_location = glGetUniformLocation(text80_program, "textport_texture");
    textport80_texture_coord_scale_location = glGetUniformLocation(text80_program, "textport_texture_coord_scale");
    textport80_aux_texture_location = glGetUniformLocation(text80_program, "textport_aux_texture");
    textport80_font_texture_location = glGetUniformLocation(text80_program, "font_texture");
    textport80_blink_location = glGetUniformLocation(text80_program, "blink");
    textport80_x_offset_location = glGetUniformLocation(text80_program, "x_offset");
    textport80_y_offset_location = glGetUniformLocation(text80_program, "y_offset");
    textport80_to_screen_location = glGetUniformLocation(text80_program, "to_screen");
    textport80_foreground_location = glGetUniformLocation(text80_program, "foreground");
    textport80_background_location = glGetUniformLocation(text80_program, "background");
    textport80_font_texture_coord_scale_location = glGetUniformLocation(text80_program, "font_texture_coord_scale");
    CheckOpenGL(__FILE__, __LINE__);

    lores_program = GenerateProgram("textport", text_vertex_shader, lores_fragment_shader);
    lores_texture_location = glGetUniformLocation(lores_program, "lores_texture");
    lores_texture_coord_scale_location = glGetUniformLocation(lores_program, "lores_texture_coord_scale");
    lores_x_offset_location = glGetUniformLocation(lores_program, "x_offset");
    lores_y_offset_location = glGetUniformLocation(lores_program, "y_offset");
    lores_to_screen_location = glGetUniformLocation(lores_program, "to_screen");
    CheckOpenGL(__FILE__, __LINE__);

    initialize_screen_areas();
    CheckOpenGL(__FILE__, __LINE__);
}

unsigned char disk_in_on_bitmap[] = {
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, // 40, 40
255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 0, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
};

unsigned char disk_out_bitmap[] = {
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, // 40, 40
255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 255, 0, 0, 0, 0, 0, 255, 255, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 0, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
};

unsigned char disk_in_off_bitmap[] = {
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, // 40, 40
255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 255, 0, 255, 0, 255, 255, 0, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 255, 0, 0, 0, 0, 0, 255, 255, 0, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 255, 0, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, 255, 255, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 0, 0, 0, 0, 255, 0, 255, 0, 255, // 40, 40
255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 255, // 40, 40
255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, // 40, 40
};

struct floppy_icon : public widget
{
    int number;
    bool inserted;
    bool active;

    switcher *switched;
    widgetbox *labeled;

    floppy_icon(int number_, bool inserted_) :
        number(number_),
        inserted(inserted_),
        active(false)
    {
        widget *disk_out = new padding(3, 3, 3, 3, new centering(new image_widget(40, 23, disk_out_bitmap)));
        widget *disk_in = new padding(3, 3, 3, 3, new centering(new image_widget(40, 23, disk_in_off_bitmap)));
        widget *disk_in_active = new padding(3, 3, 3, 3, new centering(new image_widget(40, 23, disk_in_on_bitmap)));
        switched = new switcher({disk_out, disk_in, disk_in_active});
        switched->which = inserted_ ? 1 : 0;
        widget *label = new text_widget(to_string(number_ + 1));
        labeled = new widgetbox(widgetbox::HORIZONTAL, {new centering(label), new centering((widget*)switched), new centering(new text_widget(" "))});
    }
    virtual tuple<float, float> get_min_dimensions() const
    {
        return labeled->get_min_dimensions();
    }
    virtual void draw(double now, float to_screen[9], float x, float y, float w, float h)
    {
        labeled->draw(now, to_screen, x, y, w, h);
    }
    virtual bool click(double now, float x, float y)
    {
        float w, h;
        tie(w, h) = get_min_dimensions();
        if(x >= 0 && y >= 0 & x < w && y < h)
            return true;
        return false;
    }
    virtual void hover(double now, float x, float y)
    {
    }
    virtual void drag(double now, float x, float y) 
    {
    }
    virtual void release(double now, float x, float y)
    {
        // eject
        if(inserted)
            event_queue.push_back({EJECT_FLOPPY, number});
        switched->which = 0;
    }
    virtual bool drop(double now, float x, float y, int count, const char** paths)
    {
        // insert
        float w, h;
        tie(w, h) = get_min_dimensions();
        if(x >= 0 && y >= 0 & x < w && y < h) {
            event_queue.push_back({INSERT_FLOPPY, number, strdup(paths[0])});
            switched->which = 1;
            return true;
        }
        return false;
    }
    void change_state(double now, bool inserted_, bool active_)
    {
        switched->which = inserted_ ? (active_ ? 2 : 1) : 0;
        active = active_;
        inserted = inserted_;
    }
};


floppy_icon *floppy0_icon;
floppy_icon *floppy1_icon;

void initialize_widgets(bool run_fast, bool add_floppies, bool floppy0_inserted, bool floppy1_inserted)
{
    momentary *reset_momentary = new momentary("RESET", [](){event_queue.push_back({RESET, 0});});
    momentary *reboot_momentary = new momentary("REBOOT", [](){event_queue.push_back({REBOOT, 0});});
    toggle *fast_toggle = new toggle("FAST", run_fast, [](){event_queue.push_back({SPEED, 1});}, [](){event_queue.push_back({SPEED, 0});});
    caps_toggle = new toggle("CAPS", true, [](){force_caps_on = true;}, [](){force_caps_on = false;});
    toggle *color_toggle = new toggle("COLOR", false, [](){draw_using_color = true;}, [](){draw_using_color = false;});
    toggle *pause_toggle = new toggle("PAUSE", false, [](){event_queue.push_back({PAUSE, 1});}, [](){event_queue.push_back({PAUSE, 0});});

    vector<widget*> controls = {reset_momentary, reboot_momentary, fast_toggle, caps_toggle, color_toggle, pause_toggle};
    if(add_floppies) {
        floppy0_icon = new floppy_icon(0, floppy0_inserted);
        floppy1_icon = new floppy_icon(1, floppy1_inserted);
        controls.push_back(floppy0_icon);
        controls.push_back(floppy1_icon);
    }
    vector<widget*> controls_centered;
    for(auto b : controls)
        controls_centered.push_back(new centering(b));

    widget *screen = new apple2screen();
    widget *buttonpanel = new centering(new widgetbox(widgetbox::VERTICAL, controls_centered));
    vector<widget*> panels_centered = {new spacer(10, 0), new centering(screen), new spacer(10, 0), new centering(buttonpanel), new spacer(10, 0)};

    ui = new centering(new widgetbox(widgetbox::HORIZONTAL, panels_centered));
}

void show_floppy_activity(int number, bool activity)
{
    chrono::time_point<chrono::system_clock> now = std::chrono::system_clock::now();
    chrono::duration<double> elapsed = now - start_time;
    if(number == 0)
        floppy0_icon->change_state(elapsed.count(), 1, activity);
    else if(number == 1)
        floppy1_icon->change_state(elapsed.count(), 1, activity);
}

float pixel_to_ui_scale;
float to_screen_transform[9];

void make_to_screen_transform()
{
    to_screen_transform[0 * 3 + 0] = 2.0 / gWindowWidth * pixel_to_ui_scale;
    to_screen_transform[0 * 3 + 1] = 0;
    to_screen_transform[0 * 3 + 2] = 0;
    to_screen_transform[1 * 3 + 0] = 0;
    to_screen_transform[1 * 3 + 1] = -2.0 / gWindowHeight * pixel_to_ui_scale;
    to_screen_transform[1 * 3 + 2] = 0;
    to_screen_transform[2 * 3 + 0] = -1;
    to_screen_transform[2 * 3 + 1] = 1;
    to_screen_transform[2 * 3 + 2] = 1;
}

tuple<float, float> window_to_widget(float x, float y)
{
    float wx, wy;
    wx = x / pixel_to_ui_scale;
    wy = y / pixel_to_ui_scale;

    return make_tuple(wx, wy);
}

static void redraw(GLFWwindow *window)
{
    chrono::time_point<chrono::system_clock> now = std::chrono::system_clock::now();
    chrono::duration<double> elapsed = now - start_time;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ui->draw(elapsed.count(), to_screen_transform, 0, 0, gWindowWidth / pixel_to_ui_scale, gWindowHeight / pixel_to_ui_scale);

    CheckOpenGL(__FILE__, __LINE__);
}

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW: %s\n", description);
}

static void key(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    static bool super_down = false;
    static bool caps_lock_down = false;

    // XXX not ideal, can be enqueued out of turn
    if(caps_lock_down && !force_caps_on) {
        caps_lock_down = false;
        event_queue.push_back({KEYUP, CAPS_LOCK});
    } else if(!caps_lock_down && force_caps_on) {
        caps_lock_down = true;
        event_queue.push_back({KEYDOWN, CAPS_LOCK});
    }

    if(action == GLFW_PRESS || action == GLFW_REPEAT ) {
        if(key == GLFW_KEY_RIGHT_SUPER || key == GLFW_KEY_LEFT_SUPER)
            super_down = true;
        else if(super_down && key == GLFW_KEY_V) {
            const char* text = glfwGetClipboardString(window);
            if (text)
                event_queue.push_back({PASTE, 0, strdup(text)});
        } else {
            if(key == GLFW_KEY_CAPS_LOCK) {
                force_caps_on = true;
                caps_toggle->on = true;
            }
            event_queue.push_back({KEYDOWN, key});
        }
    } else if(action == GLFW_RELEASE) {
        if(key == GLFW_KEY_RIGHT_SUPER || key == GLFW_KEY_LEFT_SUPER)
            super_down = false;
        if(key == GLFW_KEY_CAPS_LOCK) {
            force_caps_on = false;
            caps_toggle->on = false;
        }
        event_queue.push_back({KEYUP, key});
    }
}

static void resize_based_on_window(GLFWwindow *window)
{
    glfwGetWindowSize(window, &gWindowWidth, &gWindowHeight);
    float cw, ch;
    tie(cw, ch) = ui->get_min_dimensions();
    if(float(gWindowHeight) / gWindowWidth < ch / cw) {
        pixel_to_ui_scale = gWindowHeight / ch;
    } else {
        pixel_to_ui_scale = gWindowWidth / cw;
    }
    make_to_screen_transform();
}

static void resize(GLFWwindow *window, int x, int y)
{
    resize_based_on_window(window);
    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
}

widget *widget_clicked = NULL;

static void button(GLFWwindow *window, int b, int action, int mods)
{
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    chrono::time_point<chrono::system_clock> now = std::chrono::system_clock::now();
    chrono::duration<double> elapsed = now - start_time;

    if(b == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
        gButtonPressed = 1;
	gOldMouseX = x;
	gOldMouseY = y;

        float wx, wy;
        tie(wx, wy) = window_to_widget(x, y);
        if(ui->click(elapsed.count(), wx, wy)) {
            widget_clicked = ui;
        }
    } else {
        gButtonPressed = -1;
        if(widget_clicked) {
            float wx, wy;
            tie(wx, wy) = window_to_widget(x, y);
            widget_clicked->release(elapsed.count(), wx, wy);
        }
        widget_clicked = nullptr;
    }
    redraw(window);
}

static void motion(GLFWwindow *window, double x, double y)
{
    // to handle https://github.com/glfw/glfw/issues/161
    // If no motion has been reported yet, we catch the first motion
    // reported and store the current location
    if(!gMotionReported) {
        gMotionReported = true;
        gOldMouseX = x;
        gOldMouseY = y;
    }
    chrono::time_point<chrono::system_clock> now = std::chrono::system_clock::now();
    chrono::duration<double> elapsed = now - start_time;

    double dx, dy;

    dx = x - gOldMouseX;
    dy = y - gOldMouseY;

    gOldMouseX = x;
    gOldMouseY = y;

    float wx, wy;
    tie(wx, wy) = window_to_widget(x, y);

    if(gButtonPressed == 1) {
        if(widget_clicked) {
            widget_clicked->drag(elapsed.count(), wx, wy);
        }
    } else {
        ui->hover(elapsed.count(), wx, wy);
    }
    redraw(window);
}

static void scroll(GLFWwindow *window, double dx, double dy)
{
}

const int pixel_scale = 3;

void load_joystick_setup()
{
    FILE *fp = fopen("joystick.ini", "r");
    if(fp == NULL) {
        fprintf(stderr,"no joystick.ini file found, assuming defaults\n");
        fprintf(stderr,"store GLFW joystick axis 0 and 1 and button 0 and 1 in joystick.ini\n");
        fprintf(stderr,"e.g. \"3 4 12 11\" for Samsung EI-GP20\n");
        return;
    }
    if(fscanf(fp, "%d %d %d %d", &joystick_axis0, &joystick_axis1, &joystick_button0, &joystick_button1) != 4) {
        fprintf(stderr,"couldn't parse joystick.ini\n");
        fprintf(stderr,"store GLFW joystick axis 0 and 1 and button 0 and 1 in joystick.ini\n");
        fprintf(stderr,"e.g. \"3 4 12 11\" for Samsung EI-GP20\n");
    }
    fclose(fp);
}

void drop_callback(GLFWwindow* window, int count, const char** paths)
{
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    float wx, wy;
    chrono::time_point<chrono::system_clock> now = std::chrono::system_clock::now();
    chrono::duration<double> elapsed = now - start_time;
    tie(wx, wy) = window_to_widget(x, y);
    ui->drop(elapsed.count(), wx, wy, count, paths);
}

void enqueue_audio_samples(char *buf, size_t sz)
{
    ao_play(aodev, buf, sz);
}

ao_device *open_ao()
{
    ao_device *device;
    ao_sample_format format;
    int default_driver;

    ao_initialize();

    default_driver = ao_default_driver_id();

    memset(&format, 0, sizeof(format));
    format.bits = 8;
    format.channels = 1;
    format.rate = 44100;
    format.byte_format = AO_FMT_LITTLE;

    /* -- Open driver -- */
    device = ao_open_live(default_driver, &format, NULL /* no options */);
    if (device == NULL) {
        fprintf(stderr, "Error opening libao audio device.\n");
        return nullptr;
    }
    return device;
}

void start(bool run_fast, bool add_floppies, bool floppy0_inserted, bool floppy1_inserted)
{
    aodev = open_ao();
    if(aodev == NULL)
        exit(EXIT_FAILURE);

    load_joystick_setup();

    glfwSetErrorCallback(error_callback);
    start_time = std::chrono::system_clock::now();

    if(!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); 

    // glfwWindowHint(GLFW_SAMPLES, 4);
    my_window = glfwCreateWindow(280 * pixel_scale, 192 * pixel_scale, "Apple //e", NULL, NULL);
    if (!my_window) {
        glfwTerminate();
        fprintf(stdout, "Couldn't open main window\n");
        exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(my_window);
    // printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    // printf("GL_VERSION: %s\n", glGetString(GL_VERSION));

    glfwGetWindowSize(my_window, &gWindowWidth, &gWindowHeight);
    make_to_screen_transform();
    initialize_gl();
    initialize_widgets(run_fast, add_floppies, floppy0_inserted, floppy1_inserted);
    resize_based_on_window(my_window);
    CheckOpenGL(__FILE__, __LINE__);

    glfwSetKeyCallback(my_window, key);
    glfwSetMouseButtonCallback(my_window, button);
    glfwSetCursorPosCallback(my_window, motion);
    glfwSetScrollCallback(my_window, scroll);
    glfwSetFramebufferSizeCallback(my_window, resize);
    glfwSetWindowRefreshCallback(my_window, redraw);
    glfwSetDropCallback(my_window, drop_callback);
    CheckOpenGL(__FILE__, __LINE__);
}

void apply_writes(void);

void iterate()
{
    apply_writes();

    CheckOpenGL(__FILE__, __LINE__);
    if(glfwWindowShouldClose(my_window)) {
        event_queue.push_back({QUIT, 0});
    }

    CheckOpenGL(__FILE__, __LINE__);
    redraw(my_window);
    CheckOpenGL(__FILE__, __LINE__);
    glfwSwapBuffers(my_window);
    CheckOpenGL(__FILE__, __LINE__);

    if(glfwJoystickPresent(GLFW_JOYSTICK_1)) {
        if(false) printf("joystick 1 present\n");

        int axis_count, button_count;
        const float* axes = glfwGetJoystickAxes(GLFW_JOYSTICK_1, &axis_count);
        const unsigned char* buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1, &button_count);

        if(false) for(int i = 0; i < axis_count; i++)
            printf("Axis %d: %f\n", i, axes[i]);
        if(false)for(int i = 0; i < button_count; i++)
            printf("Button %d: %s\n", i, (buttons[i] == GLFW_PRESS) ? "pressed" : "not pressed");

        if(axis_count <= joystick_axis0 || axis_count <= joystick_axis1) {

            fprintf(stderr, "couldn't map joystick/gamepad axes\n");
            fprintf(stderr, "mapped joystick axes are %d and %d, but maximum axis is %d\n", joystick_axis0, joystick_axis1, axis_count);
            use_joystick = false;

        } else if(button_count <= joystick_button0 && button_count <= joystick_button1) {

            fprintf(stderr, "couldn't map joystick/gamepad buttons\n");
            fprintf(stderr, "mapped buttons are %d and %d, but maximum button is %d\n", joystick_button0, joystick_button1, button_count);
            use_joystick = false;

        } else  {

            paddle_values[0] = (axes[joystick_axis0] + 1) / 2;
            paddle_values[1] = (axes[joystick_axis1] + 1) / 2;

            paddle_buttons[0] = buttons[joystick_button0] == GLFW_PRESS;
            paddle_buttons[1] = buttons[joystick_button1] == GLFW_PRESS;
            use_joystick = true;
        }

    } else {
        use_joystick = false;
    }


    glfwPollEvents();
}

void shutdown()
{
    glfwTerminate();
}

void set_switches(DisplayMode mode_, bool mixed, int page, bool vid80_, bool altchar_)
{
    display_mode = mode_;
    mixed_mode = mixed;
    display_page = page;
    vid80 = vid80_;
    altchar = altchar_;

    // XXX
    static bool altchar_warned = false;
    if(altchar && !altchar_warned) {
        fprintf(stderr, "Warning: ALTCHAR activated, is not implemented\n");
        altchar_warned = true;
    }
}

static const int text_page1_base = 0x400;
static const int text_page2_base = 0x800;
static const int text_page_size = 0x400;
static const int hires_page1_base = 0x2000;
static const int hires_page2_base = 0x4000;
static const int hires_page_size = 8192;

extern int text_row_base_offsets[24];
extern int hires_memory_to_scanout_address[8192];

map< tuple<int, bool>, unsigned char> writes;
int collisions = 0;

void write2(int addr, bool aux, unsigned char data)
{
    // We know text page 1 and 2 are contiguous
    if((addr >= text_page1_base) && (addr < text_page2_base + text_page_size)) {
        int page = (addr >= text_page2_base) ? 1 : 0;
        int within_page = addr - text_page1_base - page * text_page_size;
        for(int row = 0; row < 24; row++) {
            int row_offset = text_row_base_offsets[row];
            if((within_page >= row_offset) && (within_page < row_offset + 40)) {
                int col = within_page - row_offset;
                glBindTexture(GL_TEXTURE_2D, textport_texture[aux ? 1 : 0][page]);
                glTexSubImage2D(GL_TEXTURE_2D, 0, col, row, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_BYTE, &data);
                CheckOpenGL(__FILE__, __LINE__);
            }
        }

    } else if(((addr >= hires_page1_base) && (addr < hires_page1_base + hires_page_size)) || ((addr >= hires_page2_base) && (addr < hires_page2_base + hires_page_size))) {

        int page = (addr < hires_page2_base) ? 0 : 1;
        int page_base = (page == 0) ? hires_page1_base : hires_page2_base;
        int within_page = addr - page_base;
        int scanout_address = hires_memory_to_scanout_address[within_page];
        int row = scanout_address / 40;
        int col = scanout_address % 40;
        glBindTexture(GL_TEXTURE_2D, hires_texture[page]);
        unsigned char pixels[8];
        for(int i = 0; i < 8 ; i++)
            pixels[i] = ((data & (1 << i)) ? 255 : 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, col * 8, row, 8, 1, GL_RED, GL_UNSIGNED_BYTE, pixels);
        CheckOpenGL(__FILE__, __LINE__);
    }
}

void apply_writes(void)
{
    for(auto it : writes) {
        int addr;
        bool aux;
        tie(addr, aux) = it.first;
        write2(addr, aux, it.second); 
    }
    writes.clear();
    collisions = 0;
}

bool write(int addr, bool aux, unsigned char data)
{
    // We know text page 1 and 2 are contiguous
    if((addr >= text_page1_base) && (addr < text_page2_base + text_page_size)) {

        if(writes.find(make_tuple(addr, aux)) != writes.end())
            collisions++;
        writes[make_tuple(addr, aux)] = data;
        return true;

    } else if(((addr >= hires_page1_base) && (addr < hires_page1_base + hires_page_size)) || ((addr >= hires_page2_base) && (addr < hires_page2_base + hires_page_size))) {

        if(writes.find(make_tuple(addr, aux)) != writes.end())
            collisions++;
        writes[make_tuple(addr, aux)] = data;
        return true;
    }
    return false;
}

int text_row_base_offsets[24] =
{
    0x000,
    0x080,
    0x100,
    0x180,
    0x200,
    0x280,
    0x300,
    0x380,
    0x028,
    0x0A8,
    0x128,
    0x1A8,
    0x228,
    0x2A8,
    0x328,
    0x3A8,
    0x050,
    0x0D0,
    0x150,
    0x1D0,
    0x250,
    0x2D0,
    0x350,
    0x3D0,
};

int hires_row_base_offsets[192] =
{
     0x0000,  0x0400,  0x0800,  0x0C00,  0x1000,  0x1400,  0x1800,  0x1C00, 
     0x0080,  0x0480,  0x0880,  0x0C80,  0x1080,  0x1480,  0x1880,  0x1C80, 
     0x0100,  0x0500,  0x0900,  0x0D00,  0x1100,  0x1500,  0x1900,  0x1D00, 
     0x0180,  0x0580,  0x0980,  0x0D80,  0x1180,  0x1580,  0x1980,  0x1D80, 
     0x0200,  0x0600,  0x0A00,  0x0E00,  0x1200,  0x1600,  0x1A00,  0x1E00, 
     0x0280,  0x0680,  0x0A80,  0x0E80,  0x1280,  0x1680,  0x1A80,  0x1E80, 
     0x0300,  0x0700,  0x0B00,  0x0F00,  0x1300,  0x1700,  0x1B00,  0x1F00, 
     0x0380,  0x0780,  0x0B80,  0x0F80,  0x1380,  0x1780,  0x1B80,  0x1F80, 
     0x0028,  0x0428,  0x0828,  0x0C28,  0x1028,  0x1428,  0x1828,  0x1C28, 
     0x00A8,  0x04A8,  0x08A8,  0x0CA8,  0x10A8,  0x14A8,  0x18A8,  0x1CA8, 
     0x0128,  0x0528,  0x0928,  0x0D28,  0x1128,  0x1528,  0x1928,  0x1D28, 
     0x01A8,  0x05A8,  0x09A8,  0x0DA8,  0x11A8,  0x15A8,  0x19A8,  0x1DA8, 
     0x0228,  0x0628,  0x0A28,  0x0E28,  0x1228,  0x1628,  0x1A28,  0x1E28, 
     0x02A8,  0x06A8,  0x0AA8,  0x0EA8,  0x12A8,  0x16A8,  0x1AA8,  0x1EA8, 
     0x0328,  0x0728,  0x0B28,  0x0F28,  0x1328,  0x1728,  0x1B28,  0x1F28, 
     0x03A8,  0x07A8,  0x0BA8,  0x0FA8,  0x13A8,  0x17A8,  0x1BA8,  0x1FA8, 
     0x0050,  0x0450,  0x0850,  0x0C50,  0x1050,  0x1450,  0x1850,  0x1C50, 
     0x00D0,  0x04D0,  0x08D0,  0x0CD0,  0x10D0,  0x14D0,  0x18D0,  0x1CD0, 
     0x0150,  0x0550,  0x0950,  0x0D50,  0x1150,  0x1550,  0x1950,  0x1D50, 
     0x01D0,  0x05D0,  0x09D0,  0x0DD0,  0x11D0,  0x15D0,  0x19D0,  0x1DD0, 
     0x0250,  0x0650,  0x0A50,  0x0E50,  0x1250,  0x1650,  0x1A50,  0x1E50, 
     0x02D0,  0x06D0,  0x0AD0,  0x0ED0,  0x12D0,  0x16D0,  0x1AD0,  0x1ED0, 
     0x0350,  0x0750,  0x0B50,  0x0F50,  0x1350,  0x1750,  0x1B50,  0x1F50, 
     0x03D0,  0x07D0,  0x0BD0,  0x0FD0,  0x13D0,  0x17D0,  0x1BD0,  0x1FD0, 
};

int hires_memory_to_scanout_address[8192];

static void initialize_memory_to_scanout() __attribute__((constructor));
void initialize_memory_to_scanout()
{
    for(int row = 0; row < 192; row++) {
        int row_address = hires_row_base_offsets[row];
        for(int byte = 0; byte < 40; byte++) {
            hires_memory_to_scanout_address[row_address + byte] = row * 40 + byte;
        }
    }
}

int font_offset = 32;
unsigned char font_bytes[96 * 7 * 8] = {
    // 32 :  
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 33 : !
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 34 : "
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 35 : #
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 36 : $
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 37 : %
    0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 38 : &
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 39 : '
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 40 : (
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 41 : )
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 42 : *
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 43 : +
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 44 : ,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 45 : -
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 46 : .
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 47 : /
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 48 : 0
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 49 : 1
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 50 : 2
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 51 : 3
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 52 : 4
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 53 : 5
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 54 : 6
    0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 55 : 7
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 56 : 8
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 57 : 9
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 58 : :
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 59 : ;
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 60 : <
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 61 : =
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 62 : >
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 63 : ?
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 64 : @
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 65 : A
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 66 : B
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 67 : C
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 68 : D
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 69 : E
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 70 : F
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 71 : G
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 72 : H
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 73 : I
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 74 : J
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 75 : K
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 76 : L
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 77 : M
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0x00,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 78 : N
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 79 : O
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 80 : P
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 81 : Q
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 82 : R
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 83 : S
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 84 : T
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 85 : U
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 86 : V
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 87 : W
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0x00,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 88 : X
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 89 : Y
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 90 : Z
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 91 : [
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 92 : backslash
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 93 : ]
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 94 : ^
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 95 : _
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    // 96 : `
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 97 : a
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 98 : b
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 99 : c
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 100 : d
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 101 : e
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 102 : f
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 103 : g
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    // 104 : h
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 105 : i
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 106 : j
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
    // 107 : k
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 108 : l
    0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 109 : m
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0x00,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 110 : n
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 111 : o
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 112 : p
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    // 113 : q
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    // 114 : r
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 115 : s
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 116 : t
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 117 : u
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 118 : v
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 119 : w
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0xFF,0x00,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 120 : x
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 121 : y
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0xFF,0x00,0x00,
    // 122 : z
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0x00,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 123 : {
    0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0xFF,0xFF,0x00,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 124 : |
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0x00,0x00,0x00,
    // 125 : }
    0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0xFF,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0xFF,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 126 : ~
    0x00,0x00,0xFF,0xFF,0x00,0xFF,0x00,
    0x00,0xFF,0x00,0xFF,0xFF,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // 127 : 
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0x00,0xFF,0x00,0xFF,0x00,0x00,
    0x00,0xFF,0x00,0xFF,0x00,0xFF,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};


};

