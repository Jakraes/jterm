#ifndef JTERM_H
#define JTERM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>

typedef enum {
  JT_EVENT_NONE,
  JT_EVENT_KEY,
  JT_EVENT_TEXT,
  JT_EVENT_PASTE,
  JT_EVENT_MOUSE,
  JT_EVENT_RESIZE,
  JT_EVENT_FOCUS_IN,
  JT_EVENT_FOCUS_OUT,
} jt_event_type_t;

typedef struct {
  jt_event_type_t type;
  union {
    struct {
      int key;
      int mods;
    } key;
    struct {
      char text[64];
    } text;
    struct {
      int button, x, y, mods;
      int state;
    } mouse;
    struct {
      int w, h;
    } resize;
  };
} jt_event_t;

typedef enum {
  JT_CURSOR_BLOCK,
  JT_CURSOR_BLOCK_BLINK,
  JT_CURSOR_BAR,
  JT_CURSOR_BAR_BLINK,
  JT_CURSOR_UNDERLINE,
  JT_CURSOR_UNDERLINE_BLINK,
} jt_cursor_shape_t;

typedef enum {
  JT_BOX_LIGHT,
  JT_BOX_HEAVY,
  JT_BOX_DOUBLE,
} jt_box_style_t;

typedef struct jt_screen jt_screen_t;
typedef struct jt_widget jt_widget_t;
typedef struct jt_theme jt_theme_t;

typedef void (*jt_timer_cb)(void *userdata);

#ifndef JT_MALLOC
#include <stdlib.h>
#define JT_MALLOC(sz) malloc(sz)
#endif
#ifndef JT_FREE
#define JT_FREE(p) free(p)
#endif
#ifndef JT_REALLOC
#define JT_REALLOC(p, sz) realloc(p, sz)
#endif

int jt_init(void);
void jt_deinit(void);
void jt_enter_alt_screen(void);
void jt_leave_alt_screen(void);
void jt_set_title(const char *title);
void jt_set_cursor_shape(jt_cursor_shape_t shape);
void jt_enable_bracketed_paste(void);
void jt_disable_bracketed_paste(void);
void jt_enable_sync_output(void);
void jt_disable_sync_output(void);
void jt_get_size(int *w, int *h);
void jt_clear(void);
void jt_present(void);
void jt_hide_cursor(void);
void jt_show_cursor(void);
void jt_set_cursor(int x, int y);

void jt_putc(int x, int y, int codepoint);
void jt_puts(int x, int y, const char *utf8);
void jt_printf(int x, int y, const char *fmt, ...);

void jt_set_fg(int r, int g, int b);
void jt_set_bg(int r, int g, int b);
void jt_set_fg_256(int idx);
void jt_set_bg_256(int idx);
void jt_reset_attr(void);

void jt_clear_region(int x1, int y1, int x2, int y2);
void jt_fill_region(int x1, int y1, int x2, int y2, int codepoint);
void jt_draw_box(int x1, int y1, int x2, int y2, jt_box_style_t style);
void jt_hline(int x, int y, int len, int ch);
void jt_vline(int x, int y, int len, int ch);
void jt_scroll_region(int y1, int y2, int lines);

int jt_poll_event(jt_event_t *ev);
int jt_wait_event(jt_event_t *ev);

jt_screen_t *jt_screen_create(int x, int y, int w, int h);
void jt_screen_destroy(jt_screen_t *s);
void jt_screen_clear(jt_screen_t *s);
void jt_screen_show(jt_screen_t *s);
void jt_screen_hide(jt_screen_t *s);
void jt_screen_raise(jt_screen_t *s);
void jt_screen_lower(jt_screen_t *s);
void jt_screen_to_top(jt_screen_t *s);
void jt_screen_to_bottom(jt_screen_t *s);
void jt_screen_move(jt_screen_t *s, int x, int y);
void jt_screen_resize(jt_screen_t *s, int w, int h);
void jt_screen_make_active(jt_screen_t *s);
jt_screen_t *jt_screen_get_active(void);
void jt_screen_save(void);
void jt_screen_restore(void);
jt_screen_t *jt_screen_at(int x, int y);
void jt_screen_foreach(void (*cb)(jt_screen_t *, void *), void *userdata);

void jt_focus_next(void);
void jt_focus_prev(void);
void jt_set_focus(jt_widget_t *w);
jt_widget_t *jt_get_focus(void);

int jt_set_timer(int ms, jt_timer_cb cb, void *userdata);
void jt_clear_timer(int id);

void jt_puts_link(int x, int y, const char *text, const char *url);

void jt_set_error_callback(void (*cb)(int err, const char *msg));
void jt_set_theme(const jt_theme_t *theme);

#ifdef __cplusplus
}
#endif

#ifdef JTERM_IMPLEMENTATION

#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#endif

#include <time.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
static HANDLE jt_stdout_h = NULL;
static HANDLE jt_stdin_h = NULL;
static DWORD jt_orig_in_mode = 0;
static DWORD jt_orig_out_mode = 0;
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
static struct termios jt_orig_termios;
#endif

static void (*jt_err_cb)(int, const char *) = NULL;
static const jt_theme_t *jt_theme = NULL;

static unsigned char jt_outbuf[262144];
static size_t jt_outpos = 0;

static void jt_flush_outbuf(void) {
  if (jt_outpos) {
    fwrite(jt_outbuf, 1, jt_outpos, stdout);
    jt_outpos = 0;
  }
}

static void jt_emit(const char *s) {
  size_t n = strlen(s);

  if (n > sizeof(jt_outbuf)) {
    jt_flush_outbuf();
    fwrite(s, 1, n, stdout);
    return;
  }

  if (jt_outpos + n > sizeof(jt_outbuf)) jt_flush_outbuf();

  memcpy(jt_outbuf + jt_outpos, s, n);
  jt_outpos += n;
}

static void jt_emit_n(const char *s, size_t n) {
  if (n > sizeof(jt_outbuf)) {
    jt_flush_outbuf();
    fwrite(s, 1, n, stdout);
    return;
  }

  if (jt_outpos + n > sizeof(jt_outbuf)) jt_flush_outbuf();

  memcpy(jt_outbuf + jt_outpos, s, n);
  jt_outpos += n;
}

static void jt_emit_int(int v) {
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "%d", v);
  jt_emit_n(buf, (size_t)n);
}

static void jt_error(int code, const char *msg) {
  if (jt_err_cb) jt_err_cb(code, msg);
}

int jt_init(void) {
#if defined(_WIN32)
  jt_stdout_h = GetStdHandle(STD_OUTPUT_HANDLE);
  jt_stdin_h = GetStdHandle(STD_INPUT_HANDLE);
  if (!jt_stdout_h || !jt_stdin_h) return 0;

  SetConsoleOutputCP(65001);
  SetConsoleCP(65001);
  _setmode(_fileno(stdout), _O_BINARY);

  GetConsoleMode(jt_stdin_h, &jt_orig_in_mode);
  GetConsoleMode(jt_stdout_h, &jt_orig_out_mode);
  DWORD inMode = jt_orig_in_mode;

  inMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
  inMode |= (ENABLE_EXTENDED_FLAGS);
  SetConsoleMode(jt_stdin_h, inMode);

  DWORD outMode = jt_orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(jt_stdout_h, outMode);
#else
  struct termios t;
  if (tcgetattr(STDIN_FILENO, &jt_orig_termios) != 0) return 0;
  t = jt_orig_termios;

  t.c_lflag &= ~(ICANON | ECHO);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
#endif

  setvbuf(stdout, NULL, _IOFBF, 1 << 20);

#ifdef JT_SYNC_DEFAULT_ON
  jt_enable_sync_output();
#endif

  atexit(jt_deinit);
  return 1;
}

void jt_deinit(void) {
#if defined(_WIN32)
  if (jt_stdin_h) SetConsoleMode(jt_stdin_h, jt_orig_in_mode);
  if (jt_stdout_h) SetConsoleMode(jt_stdout_h, jt_orig_out_mode);
#else
  tcsetattr(STDIN_FILENO, TCSANOW, &jt_orig_termios);
#endif

#ifdef JT_SYNC_DEFAULT_ON
  jt_disable_sync_output();
#endif

  jt_leave_alt_screen();
  jt_show_cursor();
  jt_reset_attr();
  jt_flush_outbuf();
  fflush(stdout);
}

void jt_enter_alt_screen(void) { jt_emit("\x1b[?1049h"); }

void jt_leave_alt_screen(void) { jt_emit("\x1b[?1049l"); }

void jt_set_title(const char *title) {
  jt_emit("\x1b]2;");
  jt_emit(title ? title : "");
  jt_emit("\x07");
}

void jt_set_cursor_shape(jt_cursor_shape_t shape) {
  int code = 0;
  switch (shape) {
    case JT_CURSOR_BLOCK:
      code = 1;
      break;
    case JT_CURSOR_BLOCK_BLINK:
      code = 0;
      break;
    case JT_CURSOR_BAR:
      code = 5;
      break;
    case JT_CURSOR_BAR_BLINK:
      code = 6;
      break;
    case JT_CURSOR_UNDERLINE:
      code = 3;
      break;
    case JT_CURSOR_UNDERLINE_BLINK:
      code = 4;
      break;
  }
  jt_emit("\x1b[");
  jt_emit_int(code);
  jt_emit(" q");
}

void jt_enable_bracketed_paste(void) { jt_emit("\x1b[?2004h"); }

void jt_disable_bracketed_paste(void) { jt_emit("\x1b[?2004l"); }

void jt_enable_sync_output(void) { jt_emit("\x1b[?2026h"); }

void jt_disable_sync_output(void) { jt_emit("\x1b[?2026l"); }

void jt_get_size(int *w, int *h) {
  int W = 0, H = 0;
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (jt_stdout_h && GetConsoleScreenBufferInfo(jt_stdout_h, &info)) {
    W = info.srWindow.Right - info.srWindow.Left + 1;
    H = info.srWindow.Bottom - info.srWindow.Top + 1;
  }
#else
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    W = ws.ws_col;
    H = ws.ws_row;
  }
#endif
  if (w) *w = W;
  if (h) *h = H;
}

void jt_clear(void) { jt_emit("\x1b[2J\x1b[H"); }
void jt_present(void) {
#ifdef JT_SYNC_DEFAULT_ON
  jt_flush_outbuf();
  fflush(stdout);
#else
  jt_flush_outbuf();
  fflush(stdout);
#endif
}
void jt_hide_cursor(void) { jt_emit("\x1b[?25l"); }
void jt_show_cursor(void) { jt_emit("\x1b[?25h"); }
void jt_set_cursor(int x, int y) {
  jt_emit("\x1b[");
  jt_emit_int(y);
  jt_emit(";");
  jt_emit_int(x);
  jt_emit("H");
}

static int jt_utf8_encode(int cp, char out[4]) {
  if (cp < 0x80) {
    out[0] = (char)cp;
    return 1;
  }

  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }

  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }

  out[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

void jt_putc(int x, int y, int codepoint) {
  char buf[4];
  int n = jt_utf8_encode(codepoint, buf);
  jt_set_cursor(x, y);
  jt_emit_n(buf, (size_t)n);
}

void jt_puts(int x, int y, const char *utf8) {
  jt_set_cursor(x, y);
  jt_emit(utf8);
}

void jt_printf(int x, int y, const char *fmt, ...) {
  jt_set_cursor(x, y);
  char tmp[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  jt_emit(tmp);
}

void jt_set_fg(int r, int g, int b) {
  jt_emit("\x1b[38;2;");
  jt_emit_int(r);
  jt_emit(";");
  jt_emit_int(g);
  jt_emit(";");
  jt_emit_int(b);
  jt_emit("m");
}
void jt_set_bg(int r, int g, int b) {
  jt_emit("\x1b[48;2;");
  jt_emit_int(r);
  jt_emit(";");
  jt_emit_int(g);
  jt_emit(";");
  jt_emit_int(b);
  jt_emit("m");
}
void jt_set_fg_256(int idx) {
  jt_emit("\x1b[38;5;");
  jt_emit_int(idx);
  jt_emit("m");
}
void jt_set_bg_256(int idx) {
  jt_emit("\x1b[48;5;");
  jt_emit_int(idx);
  jt_emit("m");
}
void jt_reset_attr(void) { jt_emit("\x1b[0m"); }

void jt_clear_region(int x1, int y1, int x2, int y2) {
  for (int y = y1; y <= y2; ++y) {
    jt_set_cursor(x1, y);
    for (int x = x1; x <= x2; ++x) jt_emit(" ");
  }
}

void jt_fill_region(int x1, int y1, int x2, int y2, int codepoint) {
  char buf[4];
  int n = jt_utf8_encode(codepoint, buf);
  for (int y = y1; y <= y2; ++y) {
    jt_set_cursor(x1, y);
    for (int x = x1; x <= x2; ++x) jt_emit_n(buf, (size_t)n);
  }
}

void jt_draw_box(int x1, int y1, int x2, int y2, jt_box_style_t style) {
  int h = (style == JT_BOX_DOUBLE) ? 0x2550
                                   : (style == JT_BOX_HEAVY ? 0x2501 : 0x2500);
  int v = (style == JT_BOX_DOUBLE) ? 0x2551
                                   : (style == JT_BOX_HEAVY ? 0x2503 : 0x2502);
  int tl = (style == JT_BOX_DOUBLE) ? 0x2554
                                    : (style == JT_BOX_HEAVY ? 0x250F : 0x250C);
  int tr = (style == JT_BOX_DOUBLE) ? 0x2557
                                    : (style == JT_BOX_HEAVY ? 0x2513 : 0x2510);
  int bl = (style == JT_BOX_DOUBLE) ? 0x255A
                                    : (style == JT_BOX_HEAVY ? 0x2517 : 0x2514);
  int br = (style == JT_BOX_DOUBLE) ? 0x255D
                                    : (style == JT_BOX_HEAVY ? 0x251B : 0x2518);

  jt_putc(x1, y1, tl);
  jt_putc(x2, y1, tr);
  jt_putc(x1, y2, bl);
  jt_putc(x2, y2, br);

  jt_hline(x1 + 1, y1, x2 - x1 - 1, h);
  jt_hline(x1 + 1, y2, x2 - x1 - 1, h);

  jt_vline(x1, y1 + 1, y2 - y1 - 1, v);
  jt_vline(x2, y1 + 1, y2 - y1 - 1, v);
}

void jt_hline(int x, int y, int len, int ch) {
  for (int i = 0; i < len; i++) jt_putc(x + i, y, ch);
}
void jt_vline(int x, int y, int len, int ch) {
  for (int i = 0; i < len; i++) jt_putc(x, y + i, ch);
}

void jt_scroll_region(int y1, int y2, int lines) {
  jt_emit("\x1b[");
  jt_emit_int(y1);
  jt_emit(";");
  jt_emit_int(y2);
  jt_emit("r");
  if (lines > 0) {
    jt_emit("\x1b[");
    jt_emit_int(lines);
    jt_emit("S");
  } else if (lines < 0) {
    jt_emit("\x1b[");
    jt_emit_int(-lines);
    jt_emit("T");
  }
  jt_emit("\x1b[r");
}

#include <errno.h>
int jt_poll_event(jt_event_t *ev) {
#if defined(_WIN32)
  (void)ev;
  return 0;
#else
  if (!ev) return 0;
  char buf[64];
  ssize_t n = read(STDIN_FILENO, buf, 63);
  if (n > 0) {
    buf[n] = '\0';
    ev->type = JT_EVENT_TEXT;
    memcpy(ev->text.text, buf, (size_t)(n + 1));
    return 1;
  }
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
  return 0;
#endif
}

int jt_wait_event(jt_event_t *ev) {
#if defined(_WIN32)
  (void)ev;
  Sleep(20);
  return 0;
#else
  for (;;) {
    int r = jt_poll_event(ev);
    if (r) return r;
    struct timespec ts = {0, 20 * 1000000};
    nanosleep(&ts, NULL);
  }
#endif
}

typedef struct jt_cell {
  char *g;
  int glen;
} jt_cell_t;
struct jt_screen {
  int x, y, w, h;
  int visible;
  jt_cell_t *cells;
  struct jt_screen *prev, *next;
};

static jt_screen_t *jt_screens_head = NULL;
static jt_screen_t *jt_active_screen = NULL;

static void jt_screen_link_top(jt_screen_t *s) {
  s->prev = NULL;
  s->next = jt_screens_head;
  if (jt_screens_head) jt_screens_head->prev = s;
  jt_screens_head = s;
}

jt_screen_t *jt_screen_create(int x, int y, int w, int h) {
  jt_screen_t *s = (jt_screen_t *)JT_MALLOC(sizeof(*s));
  if (!s) return NULL;
  s->x = x;
  s->y = y;
  s->w = w;
  s->h = h;
  s->visible = 1;
  s->prev = s->next = NULL;
  s->cells = (jt_cell_t *)JT_MALLOC((size_t)(w * h) * sizeof(jt_cell_t));
  if (!s->cells) {
    JT_FREE(s);
    return NULL;
  }
  memset(s->cells, 0, (size_t)(w * h) * sizeof(jt_cell_t));
  jt_screen_link_top(s);
  return s;
}

void jt_screen_destroy(jt_screen_t *s) {
  if (!s) return;
  if (s->prev)
    s->prev->next = s->next;
  else
    jt_screens_head = s->next;
  if (s->next) s->next->prev = s->prev;
  if (s->cells) {
    for (int i = 0; i < s->w * s->h; i++)
      if (s->cells[i].g) JT_FREE(s->cells[i].g);
    JT_FREE(s->cells);
  }
  if (jt_active_screen == s) jt_active_screen = NULL;
  JT_FREE(s);
}

void jt_screen_clear(jt_screen_t *s) {
  if (!s) return;

  for (int i = 0; i < s->w * s->h; i++) {
    if (s->cells[i].g) {
      JT_FREE(s->cells[i].g);
      s->cells[i].g = NULL;
      s->cells[i].glen = 0;
    }
  }
}

void jt_screen_show(jt_screen_t *s) {
  if (s) s->visible = 1;
}
void jt_screen_hide(jt_screen_t *s) {
  if (s) s->visible = 0;
}

void jt_screen_raise(jt_screen_t *s) {
  if (!s || !s->prev) return;
  jt_screen_t *p = s->prev;
  s->prev = p->prev;
  s->next = p;
  if (p->prev)
    p->prev->next = s;
  else
    jt_screens_head = s;
  p->prev = s;
}
void jt_screen_lower(jt_screen_t *s) {
  if (!s || !s->next) return;
  jt_screen_t *n = s->next;
  s->next = n->next;
  s->prev = n;
  if (n->next) n->next->prev = s;
  n->next = s;
}
void jt_screen_to_top(jt_screen_t *s) {
  if (!s) return;
  if (s->prev) {
    s->prev->next = s->next;
    if (s->next) s->next->prev = s->prev;
    s->prev = NULL;
    s->next = jt_screens_head;
    if (jt_screens_head) jt_screens_head->prev = s;
    jt_screens_head = s;
  }
}
void jt_screen_to_bottom(jt_screen_t *s) {
  if (!s) return;
  jt_screen_t *cur = s;
  while (cur->next) cur = cur->next;
  if (s->next) {
    s->prev->next = s->next;
    s->next->prev = s->prev;
    s->next = NULL;
    cur->next = s;
    s->prev = cur;
  }
}

void jt_screen_move(jt_screen_t *s, int x, int y) {
  if (s) {
    s->x = x;
    s->y = y;
  }
}
void jt_screen_resize(jt_screen_t *s, int w, int h) {
  if (!s) return;
  jt_cell_t *nc =
      (jt_cell_t *)JT_REALLOC(s->cells, (size_t)(w * h) * sizeof(jt_cell_t));
  if (!nc) return;
  s->cells = nc;
  s->w = w;
  s->h = h;
}

void jt_screen_make_active(jt_screen_t *s) {
  if (s) jt_active_screen = s;
}

jt_screen_t *jt_screen_get_active(void) { return jt_active_screen; }

typedef struct snapshot {
  int dummy;
} snapshot_t;
static int jt_snapshot_depth = 0;
void jt_screen_save(void) { jt_snapshot_depth++; }
void jt_screen_restore(void) {
  if (jt_snapshot_depth > 0) jt_snapshot_depth--;
}

jt_screen_t *jt_screen_at(int x, int y) {
  for (jt_screen_t *s = jt_screens_head; s; s = s->next) {
    if (!s->visible) continue;
    if (x >= s->x && y >= s->y && x < s->x + s->w && y < s->y + s->h) return s;
  }
  return NULL;
}

void jt_screen_foreach(void (*cb)(jt_screen_t *, void *), void *userdata) {
  for (jt_screen_t *s = jt_screens_head; s; s = s->next) cb(s, userdata);
}

static jt_widget_t *jt_focus_w = NULL;
struct jt_widget {
  jt_widget_t *next, *prev;
  int focusable;
  int x, y, w, h;
};
static jt_widget_t *jt_widgets_head = NULL;
static void _jt_widget_register(jt_widget_t *w) {
  if (!w) return;
  w->prev = NULL;
  w->next = jt_widgets_head;
  if (jt_widgets_head) jt_widgets_head->prev = w;
  jt_widgets_head = w;
}
static void _jt_widget_unregister(jt_widget_t *w) {
  if (!w) return;
  if (w->prev)
    w->prev->next = w->next;
  else
    jt_widgets_head = w->next;
  if (w->next) w->next->prev = w->prev;
  if (jt_focus_w == w) jt_focus_w = NULL;
}
void jt_focus_next(void) {}
void jt_focus_prev(void) {}
void jt_set_focus(jt_widget_t *w) { jt_focus_w = w; }
jt_widget_t *jt_get_focus(void) { return jt_focus_w; }

typedef struct jt_timer {
  int id;
  long long deadline_ms;
  jt_timer_cb cb;
  void *ud;
  int active;
} jt_timer_t;
static jt_timer_t jt_timers[64];
static int jt_timer_seq = 1;

static long long jt_now_ms(void) {
#if defined(_WIN32)
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  unsigned long long t =
      ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  return (long long)(t / 10000ULL);
#else
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
#endif
}

int jt_set_timer(int ms, jt_timer_cb cb, void *userdata) {
  for (int i = 0; i < 64; i++)
    if (!jt_timers[i].active) {
      jt_timers[i].active = 1;
      jt_timers[i].id = jt_timer_seq++;
      jt_timers[i].deadline_ms = jt_now_ms() + ms;
      jt_timers[i].cb = cb;
      jt_timers[i].ud = userdata;
      return jt_timers[i].id;
    }
  return 0;
}

void jt_clear_timer(int id) {
  for (int i = 0; i < 64; i++)
    if (jt_timers[i].active && jt_timers[i].id == id) {
      jt_timers[i].active = 0;
    }
}

void jt_puts_link(int x, int y, const char *text, const char *url) {
  jt_set_cursor(x, y);
  jt_emit("\x1b]8;;");
  jt_emit(url);
  jt_emit("\x07");
  jt_emit(text);
  jt_emit("\x1b]8;;\x07");
}

void jt_set_error_callback(void (*cb)(int err, const char *msg)) {
  jt_err_cb = cb;
}
void jt_set_theme(const jt_theme_t *theme) { jt_theme = theme; }

#endif

#endif
