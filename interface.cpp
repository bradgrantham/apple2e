#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <deque>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <functional>
#include <cstring>
#include <cassert>
#include <cmath>
#include <ao/ao.h>

#include "gif.h"

// implicit centering in widget? Or special centering widget?
// lines (for around toggle and momentary)
// widget which is graphics/text/lores screen
// hbox
// what is window resize / shrink policy?

#define GL_SILENCE_DEPRECATION

#if defined(__linux__)
#include <GL/glew.h>
#endif // defined(__linux__)

#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include "ui_widgets.h"

#include "gl_utility.h"

#include "interface.h"

using namespace std;

namespace APPLE2Einterface
{

constexpr uint32_t apple2_screen_width = 280;
constexpr uint32_t apple2_screen_height = 192;
constexpr int recording_scale = 2;
constexpr uint32_t recording_frame_duration_hundredths = 5;

chrono::time_point<chrono::system_clock> start_time;

static GLFWwindow* my_window;
ao_device *aodev;

bool use_joystick = false;
int joystick_axis0 = -1;
int joystick_axis1 = -1;
int joystick_button0 = -1;
int joystick_button1 = -1;

extern uint16_t font_offset;
extern const uint8_t font_bytes[96 * 7 * 8];

static int gWindowWidth, gWindowHeight;

// to handle https://github.com/glfw/glfw/issues/161
static double gMotionReported = false;

static double gOldMouseX, gOldMouseY;
static int gButtonPressed = -1;

deque<event> event_queue;

bool force_caps_on = true;
bool draw_using_color = false;

ModeSettings line_to_mode[apple2_screen_height];
ModePoint most_recent_modepoint;
vertex_array line_to_area[apple2_screen_height];

render_target *rendertarget_for_recording;

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

GLuint artifact_colors_texture;
uint8_t artifact_colors[][3] = {
    {  0,   0,   0}, //  0 "black"       -> 0,0,0,0 -> {0.000000, 0.000000, 0.000000}
    {208,   0,  50}, //  1 "red"         -> 1,0,0,0 -> {0.815901, 0.000000, 0.197238}
    { 72,  11, 255}, //  2 "dark blue"   -> 0,1,0,0 -> {0.283288, 0.043435, 1.000000}
    {255,   3, 255}, //  3 "purple"      -> 1,1,0,0 -> {1.000000, 0.015525, 1.000000}
    {  0, 134,  77}, //  4 "dark green"  -> 0,0,1,0 -> {0.000000, 0.527909, 0.302762}
    {127, 127, 127}, //  5 "gray 1"      -> 1,0,1,0 -> {0.500000, 0.500000, 0.500000}
    {  0, 145, 255}, //  6 "medium blue" -> 0,1,1,0 -> {0.000000, 0.571344, 1.000000}
    {199, 138, 255}, //  7 "light blue"  -> 1,1,1,0 -> {0.783288, 0.543435, 1.000000}
    { 55, 116,   0}, //  8 "brown"       -> 0,0,0,1 -> {0.216712, 0.456565, 0.000000}
    {255, 109,   0}, //  9 "orange"      -> 1,0,0,1 -> {1.000000, 0.428656, 0.000000}
    {127, 127, 127}, // 10 "gray 2"      -> 0,1,0,1 -> {0.500000, 0.500000, 0.500000}
    {255, 120, 177}, // 11 "pink"        -> 1,1,0,1 -> {1.000000, 0.472091, 0.697238}
    {  0, 251,   0}, // 12 "light green" -> 0,0,1,1 -> {0.000000, 0.984475, 0.000000}
    {182, 243,   0}, // 13 "yello"       -> 1,0,1,1 -> {0.716712, 0.956565, 0.000000}
    { 46, 255, 204}, // 14 "aqua"        -> 0,1,1,1 -> {0.184099, 1.000000, 0.802762}
    {255, 255, 255}, // 15 "white"       -> 1,1,1,1 -> {1.000000, 1.000000, 1.000000}
};

opengl_texture font_texture;
constexpr uint32_t fonttexture_w = 7;
constexpr uint32_t fonttexture_h = 8 * 96;

opengl_texture textport_texture[2][2]; // [aux][page]

GLuint text_program;
constexpr uint32_t textport_w = 40;
constexpr uint32_t textport_h = 24;
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
GLuint lores_artifact_colors_texture_location;
GLuint lores_texture_coord_scale_location;
GLuint lores_x_offset_location;
GLuint lores_y_offset_location;
GLuint lores_to_screen_location;

const uint32_t hires_w = 320;  // MSBit is color chooser, Apple ][ weirdness
const uint32_t hires_h = 192;
opengl_texture hires_texture[2][2]; // [aux][page]

GLuint hires_program;
GLuint hires_texture_location;
GLuint hires_texture_coord_scale_location;
GLuint hires_to_screen_location;
GLuint hires_x_offset_location;
GLuint hires_y_offset_location;

GLuint dhgr_program;
GLuint dhgr_texture_location;
GLuint dhgr_aux_texture_location;
GLuint dhgr_texture_coord_scale_location;
GLuint dhgr_to_screen_location;
GLuint dhgr_x_offset_location;
GLuint dhgr_y_offset_location;

GLuint hirescolor_program;
GLuint hirescolor_texture_location;
GLuint hirescolor_texture_coord_scale_location;
GLuint hirescolor_artifact_colors_texture_location;
GLuint hirescolor_to_screen_location;
GLuint hirescolor_x_offset_location;
GLuint hirescolor_y_offset_location;

GLuint dhgrcolor_program;
GLuint dhgrcolor_texture_location;
GLuint dhgrcolor_aux_texture_location;
GLuint dhgrcolor_texture_coord_scale_location;
GLuint dhgrcolor_artifact_colors_texture_location;
GLuint dhgrcolor_to_screen_location;
GLuint dhgrcolor_x_offset_location;
GLuint dhgrcolor_y_offset_location;

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
        return make_tuple(-1, false);
    return make_tuple(paddle_values[num], paddle_buttons[num]);
}

const uint32_t raster_coords_attrib = 0;

static const char *hires_vertex_shader = R"(
    uniform mat3 to_screen;
    in vec2 vertex_coords;
    out vec2 raster_coords;
    uniform float x_offset;
    uniform float y_offset;
    
    void main()
    {
        raster_coords = vertex_coords;
        vec3 screen_coords = to_screen * vec3(vertex_coords + vec2(x_offset, y_offset), 1);
        gl_Position = vec4(screen_coords.x, screen_coords.y, .5, 1);
    }
)";

static const char *image_fragment_shader = R"(
    in vec2 raster_coords;
    uniform vec2 image_coord_scale;
    uniform sampler2D image;
    
    out vec4 color;
    
    void main()
    {
        ivec2 tc = ivec2(raster_coords.x, raster_coords.y);
        float pixel = texture(image, raster_coords * image_coord_scale).x;
        color = vec4(pixel, pixel, pixel, 1);
    }
)";

static const char *hires_fragment_shader = R"(
    in vec2 raster_coords;
    uniform vec2 hires_texture_coord_scale;
    uniform sampler2D hires_texture;
    
    out vec4 color;
    
    void main()
    {
        int byte = int(raster_coords.x) / 7;
        int bit = int(raster_coords.x) % 7;
        int texturex = byte * 8 + bit;
        ivec2 tc = ivec2(texturex, raster_coords.y);
        float pixel = texture(hires_texture, (tc + vec2(.01f, .01f)) * hires_texture_coord_scale).x;
        color = vec4(pixel, pixel, pixel, 1);
    })";

static const char *hirescolor_fragment_shader = R"(
    in vec2 raster_coords;
    uniform vec2 hires_texture_coord_scale;
    uniform sampler2D hires_texture;
    uniform sampler1D artifact_colors_texture;
    
    out vec4 color;
    
    vec2 raster_to_texture(int x, int y)
    {
        int byte = x / 7;
        int bit = x % 7;
        int texturex = byte * 8 + bit;
        return vec2(texturex + .01f, y + .01f) * hires_texture_coord_scale; 
    }

    void main()
    {
        int x = int(raster_coords.x); 
        int y = int(raster_coords.y); 
        int colorIndex;
 
        uint left = (x < 1) ? 0u : uint(255 * texture(hires_texture, raster_to_texture(x - 1, y)).x);
        uint pixel = uint(255 * texture(hires_texture, raster_to_texture(x, y)).x);
        uint right = (x > 278) ? 0u : uint(255 * texture(hires_texture, raster_to_texture(x + 1, y)).x);
 
        if((pixel == 255u) && ((left == 255u) || (right == 255u))) { 
            /* Okay, first of all, if this pixel's on and its left or right are on, it's white. */ 
            colorIndex = 15;
        } else if((pixel == 0u) && (left == 0u) && (right == 0u)) { 
            /* If none are on, it's black */ 
            colorIndex = 0;

        } else { 

            uint even = (x % 2 == 1) ? left : pixel; 
            uint odd = (x % 2 == 1) ? pixel : right; 
            uint palette = uint(texture(hires_texture, vec2((x / 7) * 8 + 7 + .01f, raster_coords.y + .01f) * hires_texture_coord_scale).x); 
 
            if(palette == 0u) { 
                if((even == 0u) && (odd == 255u)) { 
                    colorIndex = 12; /* green */
                } else if((even == 255u) && (odd == 0u)) { 
                    colorIndex = 3; /* purple */
                } else if((even == 0u) && (odd == 0u)) { 
                    colorIndex = 0;
                } /* handled 1,1 above */ 
            } else { 
                if((even == 0u) && (odd == 255u)) { 
                    colorIndex = 9; /* orange */
                } else if((even == 255u) && (odd == 0u)) { 
                    colorIndex = 6; /* "medium" blue */
                } else if((even == 0u) && (odd == 0u)) { 
                    colorIndex = 0;
                } /* handled 1,1 above */ 
            } 
        } 
        color = texture(artifact_colors_texture, colorIndex/16.0 + .01f);
    })";

static const char *dhgr_fragment_shader = R"(
    in vec2 raster_coords;
    uniform vec2 dhgr_texture_coord_scale;
    uniform sampler2D dhgr_texture;
    uniform sampler2D dhgr_aux_texture;
    
    out vec4 color;

    int get_bit(vec2 coords)
    {
        int byte = int(coords.x) / 14;
        int page = (int(coords.x) / 7) % 2;
        int bit = int(coords.x) % 7;
        int texturex = byte * 8 + bit;
        ivec2 tc = ivec2(texturex, raster_coords.y);
        if(page == 0) {
            return int(texture(dhgr_aux_texture, (tc + vec2(.01f, .01f)) * dhgr_texture_coord_scale).x);
        } else {
            return int(texture(dhgr_texture, (tc + vec2(.01f, .01f)) * dhgr_texture_coord_scale).x);
        }
    }
    
    void main()
    {
        int bit = get_bit(raster_coords * vec2(2, 1));
        if(bit == 1)
            color = vec4(1, 1, 1, 1);
        else
            color = vec4(0, 0, 0, 1);
    })";

static const char *dhgrcolor_fragment_shader = R"(
    in vec2 raster_coords;
    uniform vec2 dhgr_texture_coord_scale;
    uniform sampler2D dhgr_texture;
    uniform sampler2D dhgr_aux_texture;
    uniform sampler1D artifact_colors_texture;
    
    out vec4 color;

    int get_bit(vec2 coords)
    {
        if(coords.x < 0)
            return 0;
        int byte = int(coords.x) / 14;
        int page = (int(coords.x) / 7) % 2;
        int bit = int(coords.x) % 7;
        int texturex = byte * 8 + bit;
        ivec2 tc = ivec2(texturex, raster_coords.y);
        if(page == 0) {
            return int(texture(dhgr_aux_texture, (tc + vec2(.01f, .01f)) * dhgr_texture_coord_scale).x);
        } else {
            return int(texture(dhgr_texture, (tc + vec2(.01f, .01f)) * dhgr_texture_coord_scale).x);
        }
    }
    
    void main()
    {
        int colorIndex;

        float actualX = raster_coords.x * 2;
        int phase = int(actualX + 1) % 4;

        int A = get_bit(raster_coords * vec2(2,1) + vec2( 0, 0));
        int B = get_bit(raster_coords * vec2(2,1) + vec2(-1, 0));
        int C = get_bit(raster_coords * vec2(2,1) + vec2(-2, 0));
        int D = get_bit(raster_coords * vec2(2,1) + vec2(-3, 0));

        if(phase == 0) {
            colorIndex = D * 2 + C * 4 + B * 8 + A * 1; /* shown in screen order */
        } else if(phase == 1) {
            colorIndex = D * 4 + C * 8 + B * 1 + A * 2; /* shown in screen order */
        } else if(phase == 2) {
            colorIndex = D * 8 + C * 1 + B * 2 + A * 4; /* shown in screen order */
        } else {
            colorIndex = D * 1 + C * 2 + B * 4 + A * 8; /* shown in screen order */
        }

        color = texture(artifact_colors_texture, colorIndex/16.0 + .01f);
    })";

static const char *text_vertex_shader = R"(
    uniform mat3 to_screen;
    in vec2 vertex_coords;
    uniform float x_offset;
    uniform float y_offset;
    out vec2 raster_coords;
    
    void main()
    {
        raster_coords = vertex_coords;
        vec3 screen_coords = to_screen * vec3(vertex_coords + vec2(x_offset, y_offset), 1);
        gl_Position = vec4(screen_coords.x, screen_coords.y, .5, 1);
    })";

// 0-31 is inverse 32-63
// 32-63 is inverse 0-31
// 64-95 is blink 32-63
// 96-127 is blink 0-31
// 128-159 is normal 32-63
// 160-191 is normal 0-31
// 192-223 is normal 32-63
// 224-255 is normal 64-95

static const char *text_fragment_shader = R"(
    in vec2 raster_coords;
    uniform int blink;
    uniform vec4 foreground;
    uniform vec4 background;
    uniform vec2 font_texture_coord_scale;
    uniform sampler2D font_texture;
    uniform vec2 textport_texture_coord_scale;
    uniform sampler2D textport_texture;
    
    out vec4 color;
    
    void main()
    {
        uint character;
        character = uint(texture(textport_texture, (uvec2(uint(raster_coords.x) / 7u, uint(raster_coords.y) / 8u) + vec2(.01f, .01f)) * textport_texture_coord_scale).x * 255.0); 
        bool inverse = false;
        if(character >= 0u && character <= 31u) {
            character = character - 0u + 32u;
            inverse = true;
        } else if(character >= 32u && character <= 63u) {
            character = character - 32u + 0u;
            inverse = true;
        } else if(character >= 64u && character <= 95u) {
            character = character - 64u + 32u; // XXX BLINK 
            inverse = blink == 1;
        } else if(character >= 96u && character <= 127u){
            character = character - 96u + 0u; // XXX BLINK 
            inverse = blink == 1;
        } else if(character >= 128u && character <= 159u)
            character = character - 128u + 32u;
        else if(character >= 160u && character <= 191u)
            character = character - 160u + 0u;
        else if(character >= 192u && character <= 223u)
            character = character - 192u + 32u;
        else if(character >= 224u && character <= 255u)
            character = character - 224u + 64u;
        else 
            character = 33u;
        uvec2 inglyph = uvec2(uint(raster_coords.x) % 7u, uint(raster_coords.y) % 8u);
        uvec2 infont = inglyph + uvec2(0, character * 8u);
        float pixel = texture(font_texture, (infont + vec2(.01f, 0.1f)) * font_texture_coord_scale).x;
        if(inverse)
            color = mix(background, foreground, 1.0 - pixel);
        else
            color = mix(background, foreground, pixel);
    })";

static const char *text80_fragment_shader = R"(
    in vec2 raster_coords;
    uniform int blink;
    uniform vec4 foreground;
    uniform vec4 background;
    uniform vec2 font_texture_coord_scale;
    uniform sampler2D font_texture;
    uniform vec2 textport_texture_coord_scale;
    uniform sampler2D textport_texture;
    uniform sampler2D textport_aux_texture;
    
    out vec4 color;
    
    void main()
    {
        uint character;
        uint x = uint(raster_coords.x * 2) / 7u; 
        if(x % 2u == 1u) 
            character = uint(texture(textport_texture, (uvec2((x - 1u) / 2u, uint(raster_coords.y) / 8u) + vec2(.01f, .01f)) * textport_texture_coord_scale).x * 255.0); 
        else 
            character = uint(texture(textport_aux_texture, (uvec2(x / 2u, uint(raster_coords.y) / 8u) + vec2(.01f, .01f)) * textport_texture_coord_scale).x * 255.0); 
        bool inverse = false;
        if(character >= 0u && character <= 31u) {
            character = character - 0u + 32u;
            inverse = true;
        } else if(character >= 32u && character <= 63u) {
            character = character - 32u + 0u;
            inverse = true;
        } else if(character >= 64u && character <= 95u) {
            character = character - 64u + 32u; // XXX BLINK 
            inverse = blink == 1;
        } else if(character >= 96u && character <= 127u){
            character = character - 96u + 0u; // XXX BLINK 
            inverse = blink == 1;
        } else if(character >= 128u && character <= 159u)
            character = character - 128u + 32u;
        else if(character >= 160u && character <= 191u)
            character = character - 160u + 0u;
        else if(character >= 192u && character <= 223u)
            character = character - 192u + 32u;
        else if(character >= 224u && character <= 255u)
            character = character - 224u + 64u;
        else 
            character = 33u;
        uvec2 inglyph = uvec2(uint(raster_coords.x * 2) % 7u, uint(raster_coords.y) % 8u);
        uvec2 infont = inglyph + uvec2(0, character * 8u);
        float pixel = texture(font_texture, (infont + vec2(.01f, 0.1f)) * font_texture_coord_scale).x;
        float value;
        if(inverse)
            color = mix(background, foreground, 1.0 - pixel);
        else
            color = mix(background, foreground, pixel);
    })";

static const char *lores_fragment_shader = R"(
    in vec2 raster_coords;
    uniform vec2 lores_texture_coord_scale;
    uniform sampler2D lores_texture;
    uniform sampler1D artifact_colors_texture;
    
    out vec4 color;
    
    void main()
    {
        uint byte;
        byte = uint(texture(lores_texture, (uvec2(uint(raster_coords.x) / 7u, uint(raster_coords.y) / 8u) + vec2(.01f, .01f)) * lores_texture_coord_scale).x * 255.0); 
        uint inglyph_y = uint(raster_coords.y) % 8u;
        uint colorIndex;
        if(inglyph_y < 4u)
            colorIndex = byte % 16u;
        else
            colorIndex = byte / 16u;
        color = texture(artifact_colors_texture, colorIndex/16.0 + .01f);
    })";

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

void initialize_screen_areas()
{
    for(uint32_t i = 0; i < apple2_screen_height; i++) {
        line_to_area[i].push_back({make_rectangle_array_buffer(0, i, apple2_screen_width, 1), raster_coords_attrib, 2, GL_FLOAT, GL_FALSE, 0});
    }
}

void set_hires_shader(float to_screen[9], const opengl_texture& texture, bool color, float x, float y)
{
    if(color) {
        glUseProgram(hirescolor_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform2f(hirescolor_texture_coord_scale_location, 1.0 / texture.w, 1.0 / texture.h);
        glUniform1i(hirescolor_texture_location, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_1D, artifact_colors_texture);
        glUniform1i(hirescolor_artifact_colors_texture_location, 1);
        CheckOpenGL(__FILE__, __LINE__);

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

void set_dhgr_shader(float to_screen[9], const opengl_texture& texture, const opengl_texture& aux_texture, bool color, float x, float y)
{
    if(color) {
        glUseProgram(dhgrcolor_program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(dhgrcolor_texture_location, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, aux_texture);
        glUniform1i(dhgrcolor_aux_texture_location, 1);

        glUniform2f(dhgrcolor_texture_coord_scale_location, 1.0 / texture.w, 1.0 / texture.h);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_1D, artifact_colors_texture);
        glUniform1i(dhgrcolor_artifact_colors_texture_location, 2);
        CheckOpenGL(__FILE__, __LINE__);

        glUniformMatrix3fv(dhgrcolor_to_screen_location, 1, GL_FALSE, to_screen);
        glUniform1f(dhgrcolor_x_offset_location, x);
        glUniform1f(dhgrcolor_y_offset_location, y);
        CheckOpenGL(__FILE__, __LINE__);

    } else {

        glUseProgram(dhgr_program);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(dhgr_texture_location, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, aux_texture);
        glUniform1i(dhgr_aux_texture_location, 1);

        glUniform2f(dhgr_texture_coord_scale_location, 1.0 / texture.w, 1.0 / texture.h);

        glUniformMatrix3fv(dhgr_to_screen_location, 1, GL_FALSE, to_screen);
        glUniform1f(dhgr_x_offset_location, x);
        glUniform1f(dhgr_y_offset_location, y);
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

void set_shader(float to_screen[9], DisplayMode display_mode, bool mixed_mode, int display_page, bool vid80, bool dhgr, int blink, float x, float y)
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

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_1D, artifact_colors_texture);
        glUniform1i(lores_artifact_colors_texture_location, 1);
        CheckOpenGL(__FILE__, __LINE__);

        glUniformMatrix3fv(lores_to_screen_location, 1, GL_FALSE, to_screen);
        glUniform1f(lores_x_offset_location, x);
        glUniform1f(lores_y_offset_location, y);

    } else if(display_mode == HIRES) {

        if(dhgr) {
            set_dhgr_shader(to_screen, hires_texture[0][display_page], hires_texture[1][0], draw_using_color, x, y);
        } else {
            set_hires_shader(to_screen, hires_texture[0][display_page], draw_using_color, x, y); // XXX should switch aux
        }

    }
}

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

    virtual width_height get_min_dimensions() const
    {
        return {apple2_screen_width, apple2_screen_height};
    }

    virtual void draw(double now, float to_screen[9], float x, float y, float w_, float h_)
    {
        w = w_;
        h = h_;
        long long elapsed_millis = now * 1000;

        for(uint32_t i = 0; i < apple2_screen_height; i++) {
            const ModeSettings& settings = line_to_mode[i];

            set_shader(to_screen, settings.mode, (i < 160) ? false : settings.mixed, settings.page, settings.vid80, settings.dhgr, (elapsed_millis / 300) % 2, x, y);
            CheckOpenGL(__FILE__, __LINE__);

            line_to_area[i].bind();
            CheckOpenGL(__FILE__, __LINE__);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            CheckOpenGL(__FILE__, __LINE__);
        }
    }

    virtual bool click(double now, float x, float y)
    {
        float w, h;
        tie(w, h) = get_min_dimensions();
        if(x >= 0 && y >= 0 && x < w && y < h) {
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
        if(x >= 0 && y >= 0 && x < w && y < h) {
            char *text;

            if((count == 1) && (paths[0][0] != '/')) {

                // Make a guess that a single string with no leading
                // '/' is actually a drag-and-dropped string.  Under Ubuntu,
                // I have verified 2018-08-05 that selected text in Chrome
                // dragged onto the application comes through this
                // callback as paths[0].

                text = strdup(paths[0]);

            } else {

                FILE *fp = fopen(paths[0], "r");
                fseek(fp, 0, SEEK_END);
                long length = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                text = (char *)malloc(length + 1);
                length = fread(text, 1, length, fp);
                fclose(fp);
                text[length] = '\0';
            }

            event_queue.push_back({PASTE, 0, text});
            return true;
        }
        return false;
    }
};

struct image_widget : public widget
{
    opengl_texture image;
    vertex_array rectangle;
    int32_t w, h;

    image_widget(int32_t w_, int32_t h_, uint8_t *buffer) :
        w(w_),
        h(h_)
    {
        image = initialize_texture(w, h, buffer);
        rectangle.push_back({make_rectangle_array_buffer(0, 0, w, h), raster_coords_attrib, 2, GL_FLOAT, GL_FALSE, 0});
    }

    virtual width_height get_min_dimensions() const
    {
        return {w, h};
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

    void set_content(const string& content_)
    {
        content = content_;
        // construct string texture
        unique_ptr<uint8_t> bytes(new uint8_t[content.size() + 1]);
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
        string_texture.load(i, 1, bytes.get());
        glDeleteBuffers(1, &rectangle[0].buffer); // XXX Sooo sloppy, vertex_array should Delete in dtor
        rectangle[0] = {make_rectangle_array_buffer(0, 0, i * 7, 8), raster_coords_attrib, 2, GL_FLOAT, GL_FALSE, 0};
    }

    text_widget(const string& content_) :
        content(content_)
    {
        set(fg, 1, 1, 1, 0);
        set(bg, 0, 0, 0, 0);

        string_texture = initialize_texture(1, 1, NULL);
        rectangle.push_back({make_rectangle_array_buffer(0, 0, 7, 8), raster_coords_attrib, 2, GL_FLOAT, GL_FALSE, 0});
        set_content(content_);
    }

    virtual width_height get_min_dimensions() const
    {
        return {content.size() * 7, 8};
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

    virtual width_height get_min_dimensions() const
    {
        float w, h;
        tie(w, h) = text_widget::get_min_dimensions();
        return {w + 3 * 2, h + 3 * 2};
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
        if(x >= 0 && y >= 0 && x < w && y < h) {
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
        on = (x >= 0 && y >= 0 && x < w && y < h);
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

    virtual width_height get_min_dimensions() const
    {
        float w, h;
        tie(w, h) = text_widget::get_min_dimensions();
        return {w + 3 * 2, h + 3 * 2};
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
        if(x >= 0 && y >= 0 && x < w && y < h) {
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
        if(x >= 0 && y >= 0 && x < w && y < h) {
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

    /**
     * Sets the boolean value, updates the UI, and calls the appropriate callback.
     */
    void set_value(bool value) {
        on = value;

        if(on) {
            set(fg, 0, 0, 0, 1);
            set(bg, 1, 1, 1, 1);
            action_on();
        } else {
            set(fg, 1, 1, 1, 1);
            set(bg, 0, 0, 0, 1);
            action_off();
        }
    }
};

struct textbox : public text_widget
{
    textbox(const string& content_):
        text_widget(content_)
    {
        set(fg, 1, 1, 1, 1);
        set(bg, 0, 0, 0, 1);
    }

    virtual width_height get_min_dimensions() const
    {
        float w, h;
        tie(w, h) = text_widget::get_min_dimensions();
        return {w + 3 * 2, h + 3 * 2};
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
        return false;
    }

    virtual void drag(double now, float x, float y)
    {
    }

    virtual void release(double now, float x, float y)
    {
    }
};

widget *ui;
widget *screen_only;
toggle *caps_toggle;
toggle *record_toggle;
textbox *speed_textbox;

void initialize_gl(void)
{
#if defined(__linux__)
    glewExperimental = true; // Needed this on NVIDIA for glGenVertexArrays?!
    glewInit();
#endif // defined(__linux__)
    GLuint va;
    glGenVertexArrays(1, &va);
    glBindVertexArray(va);

    glClearColor(0, 0, 0, 1);
    CheckOpenGL(__FILE__, __LINE__);

    glGenTextures(1, &artifact_colors_texture);
    glBindTexture(GL_TEXTURE_1D, artifact_colors_texture);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, artifact_colors);
    CheckOpenGL(__FILE__, __LINE__);
    glBindTexture(GL_TEXTURE_1D, GL_NONE);

    font_texture = initialize_texture(fonttexture_w, fonttexture_h, font_bytes);
    textport_texture[0][0] = initialize_texture(textport_w, textport_h);
    textport_texture[0][1] = initialize_texture(textport_w, textport_h);
    textport_texture[1][0] = initialize_texture(textport_w, textport_h);
    textport_texture[1][1] = initialize_texture(textport_w, textport_h);
    hires_texture[0][0] = initialize_texture(hires_w, hires_h);
    hires_texture[0][1] = initialize_texture(hires_w, hires_h);
    hires_texture[1][0] = initialize_texture(hires_w, hires_h);
    hires_texture[1][1] = initialize_texture(hires_w, hires_h);
    CheckOpenGL(__FILE__, __LINE__);

    image_program = GenerateProgram("image", hires_vertex_shader, image_fragment_shader);
    assert(image_program != 0);
    glBindAttribLocation(image_program, raster_coords_attrib, "vertex_coords");
    CheckOpenGL(__FILE__, __LINE__);

    image_texture_location = glGetUniformLocation(image_program, "image");
    image_texture_coord_scale_location = glGetUniformLocation(image_program, "image_coord_scale");
    image_to_screen_location = glGetUniformLocation(image_program, "to_screen");
    image_x_offset_location = glGetUniformLocation(image_program, "x_offset");
    image_y_offset_location = glGetUniformLocation(image_program, "y_offset");

    hires_program = GenerateProgram("hires", hires_vertex_shader, hires_fragment_shader);
    glBindAttribLocation(hires_program, raster_coords_attrib, "vertex_coords");
    assert(hires_program != 0);
    hires_texture_location = glGetUniformLocation(hires_program, "hires_texture");
    hires_texture_coord_scale_location = glGetUniformLocation(hires_program, "hires_texture_coord_scale");
    hires_to_screen_location = glGetUniformLocation(hires_program, "to_screen");
    hires_x_offset_location = glGetUniformLocation(hires_program, "x_offset");
    hires_y_offset_location = glGetUniformLocation(hires_program, "y_offset");

    hirescolor_program = GenerateProgram("hirescolor", hires_vertex_shader, hirescolor_fragment_shader);
    glBindAttribLocation(hirescolor_program, raster_coords_attrib, "vertex_coords");
    assert(hirescolor_program != 0);
    hirescolor_texture_location = glGetUniformLocation(hirescolor_program, "hires_texture");
    hirescolor_texture_coord_scale_location = glGetUniformLocation(hirescolor_program, "hires_texture_coord_scale");
    hirescolor_artifact_colors_texture_location = glGetUniformLocation(hirescolor_program, "artifact_colors_texture");
    assert(hirescolor_artifact_colors_texture_location != -1);
    hirescolor_to_screen_location = glGetUniformLocation(hirescolor_program, "to_screen");
    hirescolor_x_offset_location = glGetUniformLocation(hirescolor_program, "x_offset");
    hirescolor_y_offset_location = glGetUniformLocation(hirescolor_program, "y_offset");

    dhgr_program = GenerateProgram("dhgr", hires_vertex_shader, dhgr_fragment_shader);
    glBindAttribLocation(dhgr_program, raster_coords_attrib, "vertex_coords");
    assert(dhgr_program != 0);
    dhgr_texture_location = glGetUniformLocation(dhgr_program, "dhgr_texture");
    dhgr_aux_texture_location = glGetUniformLocation(dhgr_program, "dhgr_aux_texture");
    dhgr_texture_coord_scale_location = glGetUniformLocation(dhgr_program, "dhgr_texture_coord_scale");
    dhgr_to_screen_location = glGetUniformLocation(dhgr_program, "to_screen");
    dhgr_x_offset_location = glGetUniformLocation(dhgr_program, "x_offset");
    dhgr_y_offset_location = glGetUniformLocation(dhgr_program, "y_offset");

    dhgrcolor_program = GenerateProgram("dhgrcolor", hires_vertex_shader, dhgrcolor_fragment_shader);
    glBindAttribLocation(dhgrcolor_program, raster_coords_attrib, "vertex_coords");
    assert(dhgrcolor_program != 0);
    dhgrcolor_texture_location = glGetUniformLocation(dhgrcolor_program, "dhgr_texture");
    dhgrcolor_aux_texture_location = glGetUniformLocation(dhgrcolor_program, "dhgr_aux_texture");
    dhgrcolor_texture_coord_scale_location = glGetUniformLocation(dhgrcolor_program, "dhgr_texture_coord_scale");
    dhgrcolor_artifact_colors_texture_location = glGetUniformLocation(dhgrcolor_program, "artifact_colors_texture");
    dhgrcolor_to_screen_location = glGetUniformLocation(dhgrcolor_program, "to_screen");
    dhgrcolor_x_offset_location = glGetUniformLocation(dhgrcolor_program, "x_offset");
    dhgrcolor_y_offset_location = glGetUniformLocation(dhgrcolor_program, "y_offset");
    assert(dhgrcolor_texture_location != -1);
    assert(dhgrcolor_aux_texture_location != -1);
    assert(dhgrcolor_artifact_colors_texture_location != -1);

    text_program = GenerateProgram("textport", text_vertex_shader, text_fragment_shader);
    glBindAttribLocation(text_program, raster_coords_attrib, "vertex_coords");
    assert(text_program != 0);
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
    glBindAttribLocation(text80_program, raster_coords_attrib, "vertex_coords");
    assert(text80_program != 0);
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

    lores_program = GenerateProgram("lores", text_vertex_shader, lores_fragment_shader);
    glBindAttribLocation(lores_program, raster_coords_attrib, "vertex_coords");
    assert(lores_program != 0);
    lores_texture_location = glGetUniformLocation(lores_program, "lores_texture");
    lores_texture_coord_scale_location = glGetUniformLocation(lores_program, "lores_texture_coord_scale");
    lores_artifact_colors_texture_location = glGetUniformLocation(lores_program, "artifact_colors_texture");
    lores_x_offset_location = glGetUniformLocation(lores_program, "x_offset");
    lores_y_offset_location = glGetUniformLocation(lores_program, "y_offset");
    lores_to_screen_location = glGetUniformLocation(lores_program, "to_screen");
    CheckOpenGL(__FILE__, __LINE__);

    initialize_screen_areas();
    CheckOpenGL(__FILE__, __LINE__);
}

uint8_t disk_in_on_bitmap[] = {
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

uint8_t disk_out_bitmap[] = {
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

uint8_t disk_in_off_bitmap[] = {
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
    virtual width_height get_min_dimensions() const
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
        if(x >= 0 && y >= 0 && x < w && y < h)
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
        if(x >= 0 && y >= 0 && x < w && y < h) {
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

// Globals for GIF recording.
static GifWriter gif_writer;
static bool gif_recording = false;

/**
 * Stop recording all frames to a GIF file.
 */
static void stop_record()
{
    if (gif_recording) {
        GifEnd(&gif_writer);
        gif_recording = false;
        event_queue.push_back({WITHDRAW_ITERATION_PERIOD_REQUEST, 0});
    }
}

/**
 * Start recording all frames to a GIF file.
 */
static void start_record()
{
    if (gif_recording) {
        stop_record();
    }

    if(!rendertarget_for_recording) {
        rendertarget_for_recording = new render_target(apple2_screen_width * recording_scale, apple2_screen_height * recording_scale);
    }

    GifBegin(&gif_writer, "out.gif", apple2_screen_width * recording_scale, apple2_screen_height * recording_scale, recording_frame_duration_hundredths);
    event_queue.push_back({REQUEST_ITERATION_PERIOD_IN_MILLIS, recording_frame_duration_hundredths * 10});
    gif_recording = true;
}


floppy_icon *floppy0_icon;
floppy_icon *floppy1_icon;

uint8_t hgr_page1[8192];

void save_hgr()
{
    FILE *fp = fopen("hgr.bin", "wb");
    fwrite(hgr_page1, sizeof(hgr_page1), 1, fp);
    fclose(fp);
}

void initialize_widgets(bool run_fast, bool add_floppies, bool floppy0_inserted, bool floppy1_inserted)
{
    momentary *hgr_momentary = new momentary("SNAP HGR", [](){save_hgr();});
    momentary *reset_momentary = new momentary("RESET", [](){event_queue.push_back({RESET, 0});});
    momentary *reboot_momentary = new momentary("REBOOT", [](){event_queue.push_back({REBOOT, 0});});
    toggle *fast_toggle = new toggle("FAST", run_fast, [](){event_queue.push_back({SPEED, 1});}, [](){event_queue.push_back({SPEED, 0});});
    caps_toggle = new toggle("CAPS", true, [](){force_caps_on = true;}, [](){force_caps_on = false;});
    toggle *color_toggle = new toggle("COLOR", false, [](){draw_using_color = true;}, [](){draw_using_color = false;});
    toggle *pause_toggle = new toggle("PAUSE", false, [](){event_queue.push_back({PAUSE, 1});}, [](){event_queue.push_back({PAUSE, 0});});
    record_toggle = new toggle("RECORD", false, [](){start_record();}, [](){stop_record();});

    vector<widget*> controls = {hgr_momentary, reset_momentary, reboot_momentary, fast_toggle, caps_toggle, color_toggle, pause_toggle, record_toggle};
    
    if(true) {
        speed_textbox = new textbox("X.YYY MHz");
        controls.push_back(speed_textbox);
    }

    if(add_floppies) {
        floppy0_icon = new floppy_icon(0, floppy0_inserted);
        floppy1_icon = new floppy_icon(1, floppy1_inserted);
        controls.push_back(floppy0_icon);
        controls.push_back(floppy1_icon);
    }
    vector<widget*> controls_centered;
    for(auto b : controls)
        controls_centered.push_back(new centering(b));

    screen_only = new apple2screen();
    widget *buttonpanel = new centering(new widgetbox(widgetbox::VERTICAL, controls_centered));
    vector<widget*> panels_centered = {new spacer(10, 0), new centering(screen_only), new spacer(10, 0), new centering(buttonpanel), new spacer(10, 0)};

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
float recording_transform[9];

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

    recording_transform[0 * 3 + 0] = 2.0 / apple2_screen_width;
    recording_transform[0 * 3 + 1] = 0;
    recording_transform[0 * 3 + 2] = 0;
    recording_transform[1 * 3 + 0] = 0;
    recording_transform[1 * 3 + 1] = 2.0 / apple2_screen_height;
    recording_transform[1 * 3 + 2] = 0;
    recording_transform[2 * 3 + 0] = -1;
    recording_transform[2 * 3 + 1] = -1;
    recording_transform[2 * 3 + 2] = 1;
}

tuple<float, float> window_to_widget(float x, float y)
{
    float wx, wy;
    wx = x / pixel_to_ui_scale;
    wy = y / pixel_to_ui_scale;

    return make_tuple(wx, wy);
}

void save_rgba_to_ppm(const uint8_t *rgba8_pixels, uint32_t width, uint32_t height, const char *filename)
{
    size_t row_bytes = width * 4;

    FILE *fp = fopen(filename, "w");
    fprintf(fp, "P6 %d %d 255\n", width, height);
    for(int row = 0; row < height; row++) {
        for(int col = 0; col < width; col++) {
            fwrite(rgba8_pixels + row_bytes * row + col * 4, 1, 3, fp);
        }
    }
    fclose(fp);
}

void add_rendertarget_to_gif(double now, render_target *rt)
{
    static uint8_t image_recorded[apple2_screen_width * recording_scale * apple2_screen_height * recording_scale * 4];
    
    rt->start_rendering();

        glViewport(0, 0, apple2_screen_width * recording_scale, apple2_screen_height * recording_scale);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        screen_only->draw(now, recording_transform, 0, 0, apple2_screen_width, apple2_screen_height);

    rt->stop_rendering();

    rt->start_reading();

        glReadPixels(0, 0, apple2_screen_width * recording_scale, apple2_screen_height * recording_scale, GL_RGBA, GL_UNSIGNED_BYTE, image_recorded);

        // Enable to debug framebuffer operations by writing result to screen.ppm.
        if(false) {
            save_rgba_to_ppm(image_recorded, apple2_screen_width * recording_scale, apple2_screen_height * recording_scale, "screen.ppm");
        }

        GifWriteFrame(&gif_writer, image_recorded, apple2_screen_width * recording_scale, apple2_screen_height * recording_scale, recording_frame_duration_hundredths, 8, false);

    rt->stop_reading();
}

static void redraw(GLFWwindow *window)
{
    chrono::time_point<chrono::system_clock> now = std::chrono::system_clock::now();
    chrono::duration<double> elapsed = now - start_time;

    int fbw, fbh;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    glViewport(0, 0, fbw, fbh);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ui->draw(elapsed.count(), to_screen_transform, 0, 0, gWindowWidth / pixel_to_ui_scale, gWindowHeight / pixel_to_ui_scale);

    CheckOpenGL(__FILE__, __LINE__);

    if(gif_recording) {
        add_rendertarget_to_gif(elapsed.count(), rendertarget_for_recording);
    }
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
        } else if(super_down && key == GLFW_KEY_R) {
            if (action == GLFW_PRESS) {
                // Toggle UI, which calls the callbacks.
                record_toggle->set_value(!record_toggle->on);
            }
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

constexpr uint32_t pixel_scale = 3;

void load_joystick_setup()
{
    FILE *fp = fopen("joystick.ini", "r");

    if(fp == NULL) {
        fprintf(stderr,"no joystick.ini file found, assuming no joystick.\n");
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

void enqueue_audio_samples(uint8_t *buf, size_t sz)
{
    ao_play(aodev, (char*)buf, sz);
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
    most_recent_modepoint = make_tuple(0, ModeSettings());

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

// All the "lines" in this function are from the beginning of time, to properly set the mode for
// scanlines as they are scanned out and persisted.  E.g. frame N, line 191 through frame N+2, line 0
// should actually set the entire frame to the provided display mode.  I'm being lazy at the moment
// and just touching every line the first history record touches through the next.
// Someone could probably optimize this so that every line is only touched once, but it's also likely
// that this function will be called every 16ms of simulation time and so will only contain a couple
// of frames worth of mode changes anyway.
void map_mode_to_lines(const ModePoint& p, unsigned long long to_byte)
{
    uint64_t byte = get<0>(p);
    const ModeSettings& settings = get<1>(p);
    uint64_t line = (byte + 17029) / 65;

    uint64_t to_line = (to_byte + 17029) / 65;

    for(uint64_t l = line; l < to_line; l++) {
        uint64_t line_in_frame = l % 262;
        if(0)printf("to_byte %llu, line %llu: mode %s\n", to_byte, line_in_frame, (settings.mode == APPLE2Einterface::TEXT) ? "TEXT" : ((settings.mode == APPLE2Einterface::LORES) ? "LORES" : "HIRES"));
        if(line_in_frame < 192)
            line_to_mode[line_in_frame] = settings;
    }
}

// All the "lines" in this function are from the beginning of time, to properly set the mode for
// scanlines as they are scanned out and persisted.  E.g. frame N, line 191 through frame N+2, line 0
// should actually set the entire frame to the provided display mode.  I'm being lazy at the moment
// and just touching every line the first history record touches through the next.
// Someone could probably optimize this so that every line is only touched once, but it's also likely
// that this function will be called every 16ms of simulation time and so will only contain a couple
// of frames worth of mode changes anyway.
void map_history_to_lines(const ModeHistory& history, unsigned long long current_byte)
{
    for(size_t i = 0; (i + 1) < history.size(); i++) {
        auto& current = history[i];
        auto& next = history[i + 1];

        uint64_t byte2 = get<0>(next);

        map_mode_to_lines(current, byte2);
    }

    if(!history.empty()) {
        most_recent_modepoint = history[history.size() - 1];
    }

    map_mode_to_lines(most_recent_modepoint, current_byte);
    most_recent_modepoint = { current_byte, get<1>(most_recent_modepoint) };
}

void iterate(const ModeHistory& history, unsigned long long current_byte, float megahertz)
{
    if(speed_textbox != nullptr)
    {
        static char speed_cstr[10];
        if(megahertz >= 100000.0) {
            sprintf(speed_cstr, "very fast");
        } else if(megahertz >= 10000.0) {
            sprintf(speed_cstr, "%5.2f GHz", megahertz);
        } else if(megahertz >= 1000.0) {
            sprintf(speed_cstr, "%5.3f GHz", megahertz);
        } else if(megahertz >= 100.0) {
            sprintf(speed_cstr, "%5.1f MHz", megahertz);
        } else if(megahertz >= 10.0) {
            sprintf(speed_cstr, "%5.2f MHz", megahertz);
        } else { 
            sprintf(speed_cstr, "%5.3f MHz", megahertz);
        }
        speed_textbox->set_content(speed_cstr);
    }

    apply_writes();

    CheckOpenGL(__FILE__, __LINE__);
    if(glfwWindowShouldClose(my_window)) {
        event_queue.push_back({QUIT, 0});
    }

    map_history_to_lines(history, current_byte);

    CheckOpenGL(__FILE__, __LINE__);
    redraw(my_window);
    CheckOpenGL(__FILE__, __LINE__);
    glfwSwapBuffers(my_window);
    CheckOpenGL(__FILE__, __LINE__);

    // for(int i = 0; i < 16; i++)
        // if(glfwJoystickPresent(GLFW_JOYSTICK_1 + i))
            // printf("joy %d present\n", i);

    if(glfwJoystickPresent(GLFW_JOYSTICK_1)) {
        if(false) printf("joystick 1 present\n");

        int axis_count, button_count;
        const float* axes = glfwGetJoystickAxes(GLFW_JOYSTICK_1, &axis_count);
        const uint8_t* buttons = glfwGetJoystickButtons(GLFW_JOYSTICK_1, &button_count);

        {
            static bool checkedJoystickProbing = false;
            static bool doJoystickProbe = false;

            if(!checkedJoystickProbing) {
                doJoystickProbe = (getenv("PROBE_JOYSTICKS") != NULL);
                checkedJoystickProbing = true;
            }

            if(doJoystickProbe) {
                static bool printedJoystickProbing = false;
                if(!printedJoystickProbing) {
                    printf("Joystick probing:\n");
                    printedJoystickProbing = true;
                }
                for(int i = 0; i < axis_count; i++) {
                    if(fabsf(axes[i]) > 0.01) {
                        printf("Axis %d: %f\n", i, axes[i]);
                    }
                }
                for(int i = 0; i < button_count; i++) {
                    if(buttons[i] == GLFW_PRESS) {
                        printf("Button %d: pressed\n", i);
                    }
                }
            }
        }

        if(axis_count <= joystick_axis0 || axis_count <= joystick_axis1) {

            fprintf(stderr, "couldn't map joystick/gamepad axes\n");
            fprintf(stderr, "mapped joystick axes are %d and %d, but maximum axis is %d\n", joystick_axis0, joystick_axis1, axis_count);
            use_joystick = false;

        } else if(button_count <= joystick_button0 && button_count <= joystick_button1) {

            fprintf(stderr, "couldn't map joystick/gamepad buttons\n");
            fprintf(stderr, "mapped buttons are %d and %d, but maximum button is %d\n", joystick_button0, joystick_button1, button_count);
            use_joystick = false;

        } else if(joystick_axis0 > -1) {

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

constexpr uint16_t text_page1_base = 0x400;
constexpr uint16_t text_page2_base = 0x800;
constexpr uint16_t text_page_size = 0x400;
constexpr uint16_t hires_page1_base = 0x2000;
constexpr uint16_t hires_page2_base = 0x4000;
constexpr uint16_t hires_page_size = 8192;

extern uint16_t text_row_base_offsets[24];
extern uint16_t hires_memory_to_scanout_address[8192];

typedef pair<uint16_t, bool> address_auxpage;
map<address_auxpage, uint8_t> writes;
int collisions = 0;

void write2(uint16_t addr, bool aux, uint8_t data)
{
    // We know text page 1 and 2 are contiguous
    if((addr >= text_page1_base) && (addr < text_page2_base + text_page_size)) {
        uint16_t page = (addr >= text_page2_base) ? 1 : 0;
        uint16_t within_page = addr - text_page1_base - page * text_page_size;
        for(int row = 0; row < 24; row++) {
            uint16_t row_offset = text_row_base_offsets[row];
            if((within_page >= row_offset) && (within_page < row_offset + 40)) {
                uint16_t col = within_page - row_offset;
                glBindTexture(GL_TEXTURE_2D, textport_texture[aux ? 1 : 0][page]);
                glTexSubImage2D(GL_TEXTURE_2D, 0, col, row, 1, 1, GL_RED, GL_UNSIGNED_BYTE, &data);
                CheckOpenGL(__FILE__, __LINE__);
            }
        }

    } else if(((addr >= hires_page1_base) && (addr < hires_page1_base + hires_page_size)) || ((addr >= hires_page2_base) && (addr < hires_page2_base + hires_page_size))) {

        uint16_t page = (addr < hires_page2_base) ? 0 : 1;
        uint16_t page_base = (page == 0) ? hires_page1_base : hires_page2_base;
        uint16_t within_page = addr - page_base;
        uint16_t scanout_address = hires_memory_to_scanout_address[within_page];
        uint16_t row = scanout_address / 40;
        uint16_t col = scanout_address % 40;
        glBindTexture(GL_TEXTURE_2D, hires_texture[aux][page]);
        if(page == 0) hgr_page1[addr - 0x2000] = data; // XXX hack
        uint8_t pixels[8];
        for(int i = 0; i < 8 ; i++)
            pixels[i] = ((data & (1 << i)) ? 255 : 0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, col * 8, row, 8, 1, GL_RED, GL_UNSIGNED_BYTE, pixels);
        CheckOpenGL(__FILE__, __LINE__);
    }
}

void apply_writes(void)
{
    for(auto it : writes) {
        uint16_t addr;
        bool aux;
        tie(addr, aux) = it.first;
        write2(addr, aux, it.second); 
    }
    writes.clear();
    collisions = 0;
}

bool write(uint16_t addr, bool aux, uint8_t data)
{
    // We know text page 1 and 2 are contiguous
    if((addr >= text_page1_base) && (addr < text_page2_base + text_page_size)) {

        if(writes.find({addr, aux}) != writes.end())
            collisions++;
        writes[{addr, aux}] = data;
        return true;

    } else if(((addr >= hires_page1_base) && (addr < hires_page1_base + hires_page_size)) || ((addr >= hires_page2_base) && (addr < hires_page2_base + hires_page_size))) {

        if(writes.find({addr, aux}) != writes.end())
            collisions++;
        writes[{addr, aux}] = data;
        return true;
    }
    return false;
}

uint16_t text_row_base_offsets[24] =
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

static uint16_t hires_row_base_offsets[192] =
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

uint16_t hires_memory_to_scanout_address[8192];

static void initialize_memory_to_scanout() __attribute__((constructor));
void initialize_memory_to_scanout()
{
    for(uint16_t row = 0; row < 192; row++) {
        uint16_t row_address = hires_row_base_offsets[row];
        for(uint16_t byte = 0; byte < 40; byte++) {
            hires_memory_to_scanout_address[row_address + byte] = row * 40 + byte;
        }
    }
}

uint16_t font_offset = 32;
const uint8_t font_bytes[96 * 7 * 8] = {
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

