#pragma once
#include <SDL.h>
#include <windows.h>
namespace reagba {
// SDL_GetKeyFromScancode requires SDL's video keyboard initialization, which this
// native Win32/WebView host intentionally does not use. Translate explicitly.
inline int VirtualKey(SDL_Scancode scan) {
    if (scan >= SDL_SCANCODE_A && scan <= SDL_SCANCODE_Z) return 'A' + scan - SDL_SCANCODE_A;
    if (scan >= SDL_SCANCODE_1 && scan <= SDL_SCANCODE_9) return '1' + scan - SDL_SCANCODE_1;
    switch (scan) {
    case SDL_SCANCODE_0: return '0';
    case SDL_SCANCODE_RETURN: return VK_RETURN;
    case SDL_SCANCODE_BACKSPACE: return VK_BACK;
    case SDL_SCANCODE_SPACE: return VK_SPACE;
    case SDL_SCANCODE_UP: return VK_UP;
    case SDL_SCANCODE_DOWN: return VK_DOWN;
    case SDL_SCANCODE_LEFT: return VK_LEFT;
    case SDL_SCANCODE_RIGHT: return VK_RIGHT;
    case SDL_SCANCODE_LSHIFT: return VK_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return VK_RSHIFT;
    default: return 0;
    }
}
}
