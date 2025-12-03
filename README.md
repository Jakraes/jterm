# JTerm — Single-header cross-platform C TUI library

**JTerm** is a lightweight, header-only, STB-style terminal UI library.  
Zero dependencies, works on Linux/macOS and Windows 10/11.

```c
#define JTERM_IMPLEMENTATION
#include "jterm.h"
```

That’s all you need.

### Features
- Truecolor (24-bit) + 256-color + 16-color fallback  
- Full UTF-8 input/output (combining chars, emojis, CJK)
- Virtual overlapping screens with z-order, move/resize, show/hide  
- Double-buffered rendering with minimal terminal output  
- Alternate screen buffer, title, cursor shape, bracketed paste, sync output  
- Mouse support (click, move, wheel)  
- Non-blocking and blocking event polling  
- Timers, focus chain, save/restore screen state, hyperlinks (OSC 8)  
- Raw-mode handling.

### Quick example

```c
#include "jterm.h"

int main() {
    jt_init();
    jt_enter_alt_screen();
    jt_clear();

    jt_screen_t *win = jt_screen_create(5, 2, 30, 10);
    jt_screen_make_active(win);
    jt_draw_box(0, 0, 29, 9, JT_BOX_DOUBLE);
    jt_puts(2, 4, "Hello from JTerm!");

    jt_present();

    jt_event_t ev;
    jt_wait_event(&ev);  // wait for any key

    jt_deinit();
    return 0;
}
```

### Build
```bash
gcc main.c -o demo
```
