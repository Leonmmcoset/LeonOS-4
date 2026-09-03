#ifndef LEONOS_GLXGEARS_SDL_COMPAT_H
#define LEONOS_GLXGEARS_SDL_COMPAT_H

/*
 * The upstream gears translation unit keeps its SDL frontend for reference.
 * LeonOS does not link SDL; these compile-only definitions keep that unused
 * frontend available while the LeonOS main function drives the renderer.
 */
#include <stddef.h>
#include <stdint.h>

typedef struct SDL_Window { int unused; } SDL_Window;
typedef struct SDL_Renderer { int unused; } SDL_Renderer;
typedef struct SDL_Texture { int unused; } SDL_Texture;

typedef struct SDL_KeyboardEvent {
    struct { int scancode; } keysym;
} SDL_KeyboardEvent;

typedef struct SDL_WindowEvent {
    int event;
    int data1;
    int data2;
} SDL_WindowEvent;

typedef struct SDL_Event {
    int type;
    SDL_KeyboardEvent key;
    SDL_WindowEvent window;
} SDL_Event;

#define SDL_INIT_VIDEO 0x00000020U
#define SDL_WINDOWPOS_CENTERED 0
#define SDL_WINDOW_SHOWN 0x00000004U
#define SDL_RENDERER_SOFTWARE 0x00000002U
#define SDL_TEXTUREACCESS_STREAMING 1
#define SDL_PIXELFORMAT_ARGB8888 0
#define SDL_PIXELFORMAT_RGB565 1
#define SDL_PIXELFORMAT_RGBA5551 2
#define SDL_PIXELFORMAT_ABGR8888 3
#define SDL_QUIT 0x100
#define SDL_WINDOWEVENT 0x200
#define SDL_WINDOWEVENT_RESIZED 0x05
#define SDL_KEYDOWN 0x300
#define SDL_SCANCODE_ESCAPE 41
#define SDL_SCANCODE_P 19
#define SDL_SCANCODE_LEFT 80
#define SDL_SCANCODE_RIGHT 79
#define SDL_SCANCODE_UP 82
#define SDL_SCANCODE_DOWN 81

static inline void SDL_SetMainReady(void) {}
static inline int SDL_Init(unsigned int flags) { (void)flags; return -1; }
static inline const char *SDL_GetError(void) { return "SDL is unavailable"; }
static inline SDL_Window *SDL_CreateWindow(const char *title, int x, int y,
                                            int width, int height,
                                            unsigned int flags)
{
    (void)title; (void)x; (void)y; (void)width; (void)height; (void)flags;
    return NULL;
}
static inline SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index,
                                                 unsigned int flags)
{
    (void)window; (void)index; (void)flags;
    return NULL;
}
static inline SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer,
                                               unsigned int format,
                                               int access, int width, int height)
{
    (void)renderer; (void)format; (void)access; (void)width; (void)height;
    return NULL;
}
static inline void SDL_DestroyTexture(SDL_Texture *texture) { (void)texture; }
static inline void SDL_DestroyRenderer(SDL_Renderer *renderer) { (void)renderer; }
static inline void SDL_DestroyWindow(SDL_Window *window) { (void)window; }
static inline void SDL_Quit(void) {}
static inline uint32_t SDL_GetTicks(void) { return 0; }
static inline int SDL_PollEvent(SDL_Event *event) { (void)event; return 0; }
static inline int SDL_UpdateTexture(SDL_Texture *texture, const void *rect,
                                    const void *pixels, int pitch)
{
    (void)texture; (void)rect; (void)pixels; (void)pitch;
    return 0;
}
static inline int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                                 const void *source, const void *destination)
{
    (void)renderer; (void)texture; (void)source; (void)destination;
    return 0;
}
static inline void SDL_RenderPresent(SDL_Renderer *renderer) { (void)renderer; }

#endif
