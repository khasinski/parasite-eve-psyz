#!/usr/bin/env python3
"""Compile the production gamepad registry with SDL stubs, on Android and desktop."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'psyz/src/platform/sdl3_common.h'


class GamepadRegistryTest(unittest.TestCase):
    def test_hotplug_priority_and_removal(self):
        source = SOURCE.read_text()
        registry = source[source.index('struct Gamepad {'):source.index('void MyPadInit(')]
        stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
typedef uint32_t SDL_JoystickID;
typedef struct { int id; } SDL_Gamepad;
static SDL_Gamepad devices[40];
static int closes;
#define LEN(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define INFOF(...) ((void)0)
#define ERRORF(...) ((void)0)
#define WARNF(...) ((void)0)
static SDL_Gamepad *SDL_OpenGamepad(SDL_JoystickID id) {
    if(id == 39) return 0;
    devices[id].id=id; return &devices[id];
}
static const char *SDL_GetGamepadName(SDL_Gamepad *p) { (void)p; return "pad"; }
static void SDL_CloseGamepad(SDL_Gamepad *p) { assert(p); ++closes; }
#ifdef __ANDROID__
static bool SDL_IsJoystickVirtual(SDL_JoystickID id) { return id==1 || id==2; }
#endif
'''
        checks = r'''
int main(void) {
    AddGamepad(1); AddGamepad(2); AddGamepad(3); AddGamepad(4);
#ifdef __ANDROID__
    assert(gamepads[0].id==3 && gamepads[1].id==4);
    assert(gamepads[2].id==1 && gamepads[3].id==2);
#else
    assert(gamepads[0].id==1 && gamepads[1].id==2);
    assert(gamepads[2].id==3 && gamepads[3].id==4);
#endif
    AddGamepad(3); assert(!gamepads[4].id);
    AddGamepad(39); assert(!gamepads[4].id);
    RemoveGamepad(3); RemoveGamepad(4);
    assert(gamepads[0].id==1 && gamepads[1].id==2 && !gamepads[2].id);
    RemoveGamepad(38); assert(closes==2);
    RemoveGamepad(1); assert(gamepads[0].id==2);
    RemoveGamepad(2); assert(!gamepads[0].id);
    AddGamepad(3); AddGamepad(1); AddGamepad(4);
#ifdef __ANDROID__
    assert(gamepads[0].id==3 && gamepads[1].id==4 && gamepads[2].id==1);
#endif
    for(int i=5;i<=17;++i) AddGamepad(i);
    SDL_JoystickID last = gamepads[15].id;
    assert(last);
    AddGamepad(18); assert(gamepads[15].id==last);
    for(int i=0;i<16;++i) {
        assert(gamepads[i].dev && gamepads[i].dev->id==(int)gamepads[i].id);
        for(int j=0;j<i;++j) assert(gamepads[i].id!=gamepads[j].id);
    }
    return is_pads_init;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            test = Path(directory) / 'registry.c'
            test.write_text(stubs + registry + checks)
            for android in (False, True):
                with self.subTest(android=android):
                    executable = Path(directory) / ('android' if android else 'desktop')
                    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                                    '-Werror', *(['-D__ANDROID__'] if android else []),
                                    str(test), '-o', str(executable)], check=True)
                    subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
