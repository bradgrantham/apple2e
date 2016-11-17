#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <deque>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "interface.h"

using namespace std;

static int gWindowWidth;
static int gWindowHeight;

// to handle https://github.com/glfw/glfw/issues/161
static double gMotionReported = false;

static double gOldMouseX, gOldMouseY;
static int gButtonPressed = -1;

deque<event> event_queue;

bool interface_event_waiting()
{
    return event_queue.size() > 0;
}

event interface_dequeue_event()
{
    if(interface_event_waiting()) {
        event e = event_queue.front();
        event_queue.pop_back();
        return e;
    } else
        return {event::NONE, 0};
}

using namespace std;

float                aspectRatio = 1;

int region_w;
int region_h;
GLuint texture;
// unsigned char *texture_bytes;

void interface_setregion(int w, int h)
{
    region_w = w;
    region_h = h;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
}

#define CHECK_OPENGL(l) {int _glerr ; if((_glerr = glGetError()) != GL_NO_ERROR) printf("GL Error: %04X at %d\n", _glerr, l); }

void interface_updaterect(int x, int y, int w, int h, unsigned char *rgb)
{
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    CHECK_OPENGL(__LINE__);
}

void initialize_gl(void)
{
    glClearColor(0, 0, 0, 1);
    CHECK_OPENGL(__LINE__);

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    CHECK_OPENGL(__LINE__);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 256, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    CHECK_OPENGL(__LINE__);
    // glGenerateMipmapEXT(GL_TEXTURE_2D);
}

static void redraw(GLFWwindow *window)
{ 
    CHECK_OPENGL(__LINE__);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, region_w - 1, 0, region_h - 1, -1, 1);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushMatrix();

    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);

    glBegin(GL_TRIANGLES);

    glTexCoord2f(0, 1);
    glVertex3f(0, 0, 0);
    glTexCoord2f(1, 1);
    glVertex3f(279, 0, 0);
    glTexCoord2f(1, 0);
    glVertex3f(279, 191, 0);

    glTexCoord2f(0, 1);
    glVertex3f(0, 0, 0);
    glTexCoord2f(1, 0);
    glVertex3f(279, 191, 0);
    glTexCoord2f(0, 0);
    glVertex3f(0, 191, 0);

    glEnd();

    glPopMatrix();
    CHECK_OPENGL(__LINE__);
}

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW: %s\n", description);
}

static void key(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    if(action == GLFW_PRESS) {
        event_queue.push_back({event::KEYDOWN, key});
    } else if(action == GLFW_RELEASE) {
        event_queue.push_back({event::KEYUP, key});
    }
}

static void resize(GLFWwindow *window, int x, int y)
{
    glfwGetFramebufferSize(window, &gWindowWidth, &gWindowHeight);
    glViewport(0, 0, gWindowWidth, gWindowWidth);
}

static void button(GLFWwindow *window, int b, int action, int mods)
{
    double x, y;
    glfwGetCursorPos(window, &x, &y);

    if(b == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
        gButtonPressed = 1;
	gOldMouseX = x;
	gOldMouseY = y;
    } else {
        gButtonPressed = -1;
    }
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

    double dx, dy;

    dx = x - gOldMouseX;
    dy = y - gOldMouseY;

    gOldMouseX = x;
    gOldMouseY = y;

    if(gButtonPressed == 1) {
        // mouse does nothing
    }
}

static void scroll(GLFWwindow *window, double dx, double dy)
{
}

static GLFWwindow* window;

const int pixel_scale = 2;

void interface_start()
{
    glfwSetErrorCallback(error_callback);

    if(!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_SAMPLES, 4);
    window = glfwCreateWindow(gWindowWidth = 280 * pixel_scale, gWindowHeight = 192 * pixel_scale, "Apple //e", NULL, NULL);
    if (!window) {
        glfwTerminate();
        fprintf(stdout, "Couldn't open main window\n");
        exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(window);

    GLenum err = glewInit();
    if (GLEW_OK != err) {
        fprintf(stderr, "Error: %s\n", glewGetErrorString(err));
        exit(EXIT_FAILURE);
    }
    fprintf(stdout, "Status: Using GLEW %s\n", glewGetString(GLEW_VERSION));

    initialize_gl();

    glfwSetKeyCallback(window, key);
    glfwSetMouseButtonCallback(window, button);
    glfwSetCursorPosCallback(window, motion);
    glfwSetScrollCallback(window, scroll);
    glfwSetFramebufferSizeCallback(window, resize);
    glfwSetWindowRefreshCallback(window, redraw);
}

void interface_iterate()
{
    if(glfwWindowShouldClose(window)) {
        event_queue.push_back({event::QUIT, 0});
    }

    redraw(window);
    glfwSwapBuffers(window);

    glfwPollEvents();
}

void interface_shutdown()
{
    glfwTerminate();
}
