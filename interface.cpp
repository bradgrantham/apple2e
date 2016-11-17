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

void initialize_gl(void)
{
    glClearColor(0, 0, 0, 1);
}

#define CHECK_OPENGL(l) {int _glerr ; if((_glerr = glGetError()) != GL_NO_ERROR) printf("GL Error: %04X at %d\n", _glerr, l); }

static void redraw(GLFWwindow *window)
{ 
    CHECK_OPENGL(__LINE__);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, gWindowWidth - 1, 0, gWindowHeight - 1, -1, 1);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushMatrix();

    glPopMatrix();
    CHECK_OPENGL(__LINE__);
}

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW: %s\n", description);
}

static void key(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    printf("key %d(%c), scancode %d, action %d, mods %02X\n", key, isprint(key) ? key : '?', scancode, action, mods);
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

void interface_start()
{
    glfwSetErrorCallback(error_callback);

    if(!glfwInit())
        exit(EXIT_FAILURE);

    glfwWindowHint(GLFW_SAMPLES, 4);
    window = glfwCreateWindow(gWindowWidth = 280 * 4, gWindowHeight = 192 * 4, "Apple //e", NULL, NULL);
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
        // set to quit
    }

    redraw(window);

    glfwSwapBuffers(window);

    glfwPollEvents();
}

void interface_shutdown()
{
    glfwTerminate();
}
