/*
 * demo_rdoc.c — minimal terminal-based editor exercising rdoc
 *
 * Build: cc -std=c99 -Wall -Wextra -o demo_rdoc sum_tree/demo_rdoc.c
 * Usage: ./demo_rdoc
 * Quit:  Ctrl+C
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* libc_allocator is not a compile-time constant in C99, provide struct literal */
#define STREE_ALLOCATOR {.alloc = libc_alloc, .free = libc_free, .ctx = NULL}

#include "rdoc.h"

/* ========================================================================
 * Key event types
 * ======================================================================== */

typedef enum {
  KEY_CHAR,
  KEY_ENTER,
  KEY_BACKSPACE,
  KEY_ARROW_UP,
  KEY_ARROW_DOWN,
  KEY_ARROW_LEFT,
  KEY_ARROW_RIGHT,
  KEY_SHIFT_ARROW_UP,
  KEY_SHIFT_ARROW_DOWN,
  KEY_SHIFT_ARROW_LEFT,
  KEY_SHIFT_ARROW_RIGHT,
  KEY_CTRL_B,
  KEY_CTRL_I,
  KEY_CTRL_K,
  KEY_CTRL_C,
  KEY_UNKNOWN
} key_kind;

typedef struct {
  key_kind kind;
  char     value;  /* printable character (only valid when kind == KEY_CHAR) */
} key_event;

/* ========================================================================
 * Editor state
 * ======================================================================== */

typedef struct {
  rdoc     doc;
  size_t   cursor_pos;   /* codepoint offset into text (== byte offset for ASCII) */
  size_t   anchor_pos;   /* selection anchor (== cursor_pos when no selection) */
  uint32_t next_id;      /* monotonic counter for block/inline IDs */
  size_t   cursor_raw_hint; /* raw byte position hint for inserting inside blocks (0=unused) */
} editor_state;

/* ========================================================================
 * Terminal raw mode
 * ======================================================================== */

static struct termios orig_termios;
static int            raw_mode_active = 0;

static void disable_raw_mode(void) {
  if (raw_mode_active) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    raw_mode_active = 0;
  }
}

static void enable_raw_mode(void) {
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
    perror("tcgetattr");
    exit(1);
  }

  struct termios raw = orig_termios;
  raw.c_iflag &= (unsigned long)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= (unsigned long)~(OPOST);
  raw.c_cflag |= (unsigned long)(CS8);
  raw.c_lflag &= (unsigned long)~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN]  = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    perror("tcsetattr");
    exit(1);
  }

  raw_mode_active = 1;
}

/* ========================================================================
 * Signal handling
 * ======================================================================== */

static void signal_handler(int sig) {
  (void)sig;
  disable_raw_mode();
  /* Clear screen and move cursor home before exiting */
  write(STDOUT_FILENO, "\033[2J\033[H", 7);
  _exit(0);
}

static void setup_signals(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT,  &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}

/* ========================================================================
 * Keypress reader
 * ======================================================================== */

static key_event read_key(void) {
  key_event ev;
  ev.kind  = KEY_UNKNOWN;
  ev.value = 0;

  unsigned char c;
  if (read(STDIN_FILENO, &c, 1) != 1) {
    return ev;
  }

  /* Ctrl+letter detection (Ctrl masks to 0x01-0x1A) */
  if (c == 2)  { ev.kind = KEY_CTRL_B; return ev; }  /* Ctrl+B */
  if (c == 9)  { ev.kind = KEY_CTRL_I; return ev; }  /* Ctrl+I (Tab) */
  if (c == 11) { ev.kind = KEY_CTRL_K; return ev; }  /* Ctrl+K */
  if (c == 3)  { ev.kind = KEY_CTRL_C; return ev; }  /* Ctrl+C */

  /* Enter */
  if (c == 13 || c == 10) { ev.kind = KEY_ENTER; return ev; }

  /* Backspace (127 = DEL, 8 = BS) */
  if (c == 127 || c == 8) { ev.kind = KEY_BACKSPACE; return ev; }

  /* Escape sequence */
  if (c == 27) {
    unsigned char seq[5];
    /* Read with a short timeout — if nothing follows, it's bare Escape */
    /* Use non-blocking read: set a brief VTIME */
    struct termios tmp;
    tcgetattr(STDIN_FILENO, &tmp);
    struct termios timed = tmp;
    timed.c_cc[VMIN]  = 0;
    timed.c_cc[VTIME] = 1;  /* 100ms timeout */
    tcsetattr(STDIN_FILENO, TCSANOW, &timed);

    ssize_t n = read(STDIN_FILENO, &seq[0], 1);
    if (n <= 0) {
      tcsetattr(STDIN_FILENO, TCSANOW, &tmp);
      return ev;  /* bare Escape */
    }

    if (seq[0] == '[') {
      n = read(STDIN_FILENO, &seq[1], 1);
      tcsetattr(STDIN_FILENO, TCSANOW, &tmp);
      if (n <= 0) return ev;

      /* Check for extended sequences: \033[1;2X (shift+arrow) */
      if (seq[1] == '1') {
        unsigned char rest[3];
        /* Temporarily set timed mode again for reading rest */
        tcsetattr(STDIN_FILENO, TCSANOW, &timed);
        ssize_t r = read(STDIN_FILENO, rest, 3);
        tcsetattr(STDIN_FILENO, TCSANOW, &tmp);
        if (r == 3 && rest[0] == ';' && rest[1] == '2') {
          switch (rest[2]) {
            case 'A': ev.kind = KEY_SHIFT_ARROW_UP;    return ev;
            case 'B': ev.kind = KEY_SHIFT_ARROW_DOWN;  return ev;
            case 'C': ev.kind = KEY_SHIFT_ARROW_RIGHT; return ev;
            case 'D': ev.kind = KEY_SHIFT_ARROW_LEFT;  return ev;
          }
        }
        return ev;  /* unknown extended sequence */
      }

      /* Simple arrow keys: \033[A, \033[B, \033[C, \033[D */
      switch (seq[1]) {
        case 'A': ev.kind = KEY_ARROW_UP;    return ev;
        case 'B': ev.kind = KEY_ARROW_DOWN;  return ev;
        case 'C': ev.kind = KEY_ARROW_RIGHT; return ev;
        case 'D': ev.kind = KEY_ARROW_LEFT;  return ev;
      }
    } else {
      tcsetattr(STDIN_FILENO, TCSANOW, &tmp);
    }

    return ev;  /* unknown escape sequence */
  }

  /* Printable character */
  if (c >= 32 && c < 127) {
    ev.kind  = KEY_CHAR;
    ev.value = (char)c;
    return ev;
  }

  return ev;
}

/* ========================================================================
 * Cursor helpers (for up/down arrow movement)
 * ======================================================================== */

/* Compute line and column for a codepoint offset */
static void cursor_to_line_col(rdoc doc, size_t pos,
                                size_t *out_line, size_t *out_col) {
  size_t text_len = rdoc_text_byte_count(doc);
  size_t line = 0, col = 0;

  if (text_len > 0) {
    uint8_t *buf = (uint8_t *)malloc(text_len);
    rdoc_text_to_buf(doc, buf, text_len);
    size_t limit = pos < text_len ? pos : text_len;
    for (size_t i = 0; i < limit; i++) {
      if (buf[i] == '\n') { line++; col = 0; }
      else col++;
    }
    free(buf);
  }

  *out_line = line;
  *out_col = col;
}

/* Convert line+col to codepoint offset (clamps col to end of line) */
static size_t line_col_to_offset(rdoc doc, size_t target_line, size_t target_col) {
  size_t text_len = rdoc_text_byte_count(doc);
  if (text_len == 0) return 0;

  uint8_t *buf = (uint8_t *)malloc(text_len);
  rdoc_text_to_buf(doc, buf, text_len);

  /* Find start of target line */
  size_t line = 0, line_start = 0;
  for (size_t i = 0; i < text_len && line < target_line; i++) {
    if (buf[i] == '\n') { line++; line_start = i + 1; }
  }

  /* Find end of target line */
  size_t line_end = text_len;
  for (size_t i = line_start; i < text_len; i++) {
    if (buf[i] == '\n') { line_end = i; break; }
  }

  size_t line_len = line_end - line_start;
  size_t col = target_col < line_len ? target_col : line_len;

  free(buf);
  return line_start + col;
}

/* Count newlines in document (number of lines = newlines + 1) */
static size_t count_newlines(rdoc doc) {
  size_t text_len = rdoc_text_byte_count(doc);
  if (text_len == 0) return 0;

  uint8_t *buf = (uint8_t *)malloc(text_len);
  rdoc_text_to_buf(doc, buf, text_len);

  size_t count = 0;
  for (size_t i = 0; i < text_len; i++) {
    if (buf[i] == '\n') count++;
  }

  free(buf);
  return count;
}

/* ========================================================================
 * Raw-position text insertion (for typing inside blocks)
 * ======================================================================== */

static rdoc insert_text_at_raw(rdoc d, size_t raw_pos, const uint8_t *str, size_t len) {
  rdoc inserted = rdoc_from_bytes(str, len);
  rdoc_st_split_result sp = rdoc_st_split(d.root, raw_pos);
  rdoc_st_root tmp = rdoc_st_concat(sp.left, inserted.root);
  rdoc_st_root result = rdoc_st_concat(tmp, sp.right);
  rdoc_st_unref(sp.left);
  rdoc_st_unref(sp.right);
  rdoc_st_unref(tmp);
  rdoc_unref(inserted);
  return (rdoc){.root = result};
}

/* Convert cursor_pos (char offset) to raw byte offset */
static size_t cursor_to_raw(rdoc doc, size_t cursor_pos) {
  size_t char_count = rdoc_char_count(doc);
  if (cursor_pos >= char_count) return rdoc_total_bytes(doc);
  rdoc_seek_result sr = rdoc_char_to_raw(doc, cursor_pos);
  return sr.raw_index;
}

/* ========================================================================
 * Empty inline cleanup (app layer)
 * ======================================================================== */

/* Scan for empty inline blocks (INLINE_OPEN immediately followed by
   INLINE_CLOSE with the same ID) and remove them. */
static void cleanup_empty_inlines(editor_state *s) {
  for (;;) {
    uint32_t empty_id = 0;
    int found = 0;
    rdoc_cursor c = rdoc_cursor_new(s->doc, 0);
    if (!c.valid) break;
    do {
      rdoc_token tok = rdoc_cursor_token(&c);
      if (tok.kind == RDOC_TOKEN_INLINE_OPEN) {
        uint32_t id = tok.id;
        if (rdoc_cursor_advance(&c)) {
          rdoc_token next = rdoc_cursor_token(&c);
          if (next.kind == RDOC_TOKEN_INLINE_CLOSE && next.id == id) {
            empty_id = id;
            found = 1;
            break;
          }
        } else {
          break;
        }
      }
    } while (rdoc_cursor_advance(&c));
    rdoc_cursor_free(c);

    if (!found) break;
    rdoc new_doc = rdoc_remove_inline(s->doc, empty_id);
    rdoc_unref(s->doc);
    s->doc = new_doc;
  }
}

/* ========================================================================
 * Key handling
 * ======================================================================== */

static void handle_key(editor_state *s, key_event ev) {
  switch (ev.kind) {
    case KEY_CHAR: {
      uint8_t ch = (uint8_t)ev.value;
      rdoc new_doc;
      if (s->cursor_raw_hint > 0) {
        new_doc = insert_text_at_raw(s->doc, s->cursor_raw_hint, &ch, 1);
        s->cursor_raw_hint++;
      } else {
        new_doc = rdoc_insert_text(s->doc, s->cursor_pos, &ch, 1);
      }
      rdoc_unref(s->doc);
      s->doc = new_doc;
      s->cursor_pos++;
      s->anchor_pos = s->cursor_pos;
      break;
    }
    case KEY_ENTER: {
      uint8_t nl = '\n';
      rdoc new_doc;
      if (s->cursor_raw_hint > 0) {
        new_doc = insert_text_at_raw(s->doc, s->cursor_raw_hint, &nl, 1);
        s->cursor_raw_hint++;
      } else {
        new_doc = rdoc_insert_text(s->doc, s->cursor_pos, &nl, 1);
      }
      rdoc_unref(s->doc);
      s->doc = new_doc;
      s->cursor_pos++;
      s->anchor_pos = s->cursor_pos;
      break;
    }
    case KEY_BACKSPACE: {
      if (s->cursor_pos > 0) {
        rdoc new_doc = rdoc_delete_text(s->doc, s->cursor_pos - 1, 1);
        rdoc_unref(s->doc);
        s->doc = new_doc;
        s->cursor_pos--;
        if (s->cursor_raw_hint > 0) s->cursor_raw_hint--;
        cleanup_empty_inlines(s);
      }
      s->anchor_pos = s->cursor_pos;
      break;
    }
    case KEY_ARROW_LEFT: {
      if (s->cursor_pos > 0) s->cursor_pos--;
      s->anchor_pos = s->cursor_pos;
      s->cursor_raw_hint = 0;
      break;
    }
    case KEY_ARROW_RIGHT: {
      if (s->cursor_pos < rdoc_char_count(s->doc)) s->cursor_pos++;
      s->anchor_pos = s->cursor_pos;
      s->cursor_raw_hint = 0;
      break;
    }
    case KEY_ARROW_UP: {
      size_t line, col;
      cursor_to_line_col(s->doc, s->cursor_pos, &line, &col);
      if (line > 0) {
        s->cursor_pos = line_col_to_offset(s->doc, line - 1, col);
      } else {
        s->cursor_pos = 0;
      }
      s->anchor_pos = s->cursor_pos;
      s->cursor_raw_hint = 0;
      break;
    }
    case KEY_ARROW_DOWN: {
      size_t line, col;
      cursor_to_line_col(s->doc, s->cursor_pos, &line, &col);
      size_t newlines = count_newlines(s->doc);
      if (line < newlines) {
        s->cursor_pos = line_col_to_offset(s->doc, line + 1, col);
      } else {
        s->cursor_pos = rdoc_char_count(s->doc);
      }
      s->anchor_pos = s->cursor_pos;
      s->cursor_raw_hint = 0;
      break;
    }
    case KEY_SHIFT_ARROW_LEFT: {
      if (s->cursor_pos > 0) s->cursor_pos--;
      /* anchor_pos NOT updated — extends selection */
      s->cursor_raw_hint = 0;
      break;
    }
    case KEY_SHIFT_ARROW_RIGHT: {
      if (s->cursor_pos < rdoc_char_count(s->doc)) s->cursor_pos++;
      s->cursor_raw_hint = 0;
      break;
    }
    case KEY_SHIFT_ARROW_UP: {
      size_t line, col;
      cursor_to_line_col(s->doc, s->cursor_pos, &line, &col);
      if (line > 0) {
        s->cursor_pos = line_col_to_offset(s->doc, line - 1, col);
      } else {
        s->cursor_pos = 0;
      }
      s->cursor_raw_hint = 0;
      break;
    }
    case KEY_SHIFT_ARROW_DOWN: {
      size_t line, col;
      cursor_to_line_col(s->doc, s->cursor_pos, &line, &col);
      size_t newlines = count_newlines(s->doc);
      if (line < newlines) {
        s->cursor_pos = line_col_to_offset(s->doc, line + 1, col);
      } else {
        s->cursor_pos = rdoc_char_count(s->doc);
      }
      s->cursor_raw_hint = 0;
      break;
    }
    case KEY_CTRL_B: {
      /* Toggle bold on selection; no-op if no selection */
      if (s->cursor_pos != s->anchor_pos) {
        size_t start = s->cursor_pos < s->anchor_pos ? s->cursor_pos : s->anchor_pos;
        size_t end   = s->cursor_pos > s->anchor_pos ? s->cursor_pos : s->anchor_pos;
        rdoc new_doc = rdoc_toggle_mark(s->doc, start, end, 1);
        rdoc_unref(s->doc);
        s->doc = new_doc;
        s->anchor_pos = s->cursor_pos;
      }
      s->cursor_raw_hint = 0;
      break;
    }
    case KEY_CTRL_K: {
      if (s->cursor_pos != s->anchor_pos) {
        /* Wrap selection in a block */
        size_t start = s->cursor_pos < s->anchor_pos ? s->cursor_pos : s->anchor_pos;
        size_t end   = s->cursor_pos > s->anchor_pos ? s->cursor_pos : s->anchor_pos;
        size_t start_raw = cursor_to_raw(s->doc, start);
        size_t end_raw   = cursor_to_raw(s->doc, end);
        rdoc new_doc = rdoc_wrap_block(s->doc, start_raw, end_raw, s->next_id++);
        rdoc_unref(s->doc);
        s->doc = new_doc;
        s->anchor_pos = s->cursor_pos;
        s->cursor_raw_hint = 0;
      } else {
        /* Insert empty block at cursor */
        size_t raw_pos = cursor_to_raw(s->doc, s->cursor_pos);
        rdoc new_doc = rdoc_insert_block(s->doc, raw_pos, s->next_id++);
        rdoc_unref(s->doc);
        s->doc = new_doc;
        /* Set raw hint to position inside block (after BLOCK_OPEN) */
        s->cursor_raw_hint = raw_pos + 5;
      }
      break;
    }
    case KEY_CTRL_I: {
      /* Wrap selection in inline block; no-op if no selection */
      if (s->cursor_pos != s->anchor_pos) {
        size_t start = s->cursor_pos < s->anchor_pos ? s->cursor_pos : s->anchor_pos;
        size_t end   = s->cursor_pos > s->anchor_pos ? s->cursor_pos : s->anchor_pos;
        size_t start_raw = cursor_to_raw(s->doc, start);
        size_t end_raw   = cursor_to_raw(s->doc, end);
        rdoc new_doc = rdoc_wrap_inline(s->doc, start_raw, end_raw, s->next_id++);
        rdoc_unref(s->doc);
        s->doc = new_doc;
        s->anchor_pos = s->cursor_pos;
      }
      s->cursor_raw_hint = 0;
      break;
    }
    default:
      break;
  }
}

/* ========================================================================
 * Rendering — immediate-mode token cursor iteration
 * ======================================================================== */

static char _render_buf[65536];
static size_t _render_len;

static void render_flush(void) {
  if (_render_len > 0) {
    write(STDOUT_FILENO, _render_buf, _render_len);
    _render_len = 0;
  }
}

static void render_emit(const char *data, size_t len) {
  while (len > 0) {
    size_t space = sizeof(_render_buf) - _render_len;
    size_t chunk = len < space ? len : space;
    memcpy(_render_buf + _render_len, data, chunk);
    _render_len += chunk;
    if (_render_len == sizeof(_render_buf)) render_flush();
    data += chunk;
    len -= chunk;
  }
}

static void render_str(const char *s) {
  render_emit(s, strlen(s));
}

/* Emit ANSI reset + reapply current style */
static void render_style(int bold, int cyan, int reverse) {
  render_str("\033[0m");
  if (bold) render_str("\033[1m");
  if (cyan) render_str("\033[36m");
  if (reverse) render_str("\033[7m");
}

/* Emit "│ " for each depth level */
static void render_depth_prefix(int depth, int *col) {
  for (int d = 0; d < depth; d++) {
    render_emit("\xe2\x94\x82 ", 4); /* "│ " */
    *col += 2;
  }
}

static int get_term_rows(void) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0)
    return ws.ws_row;
  return 24; /* fallback */
}

static int get_term_cols(void) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    return ws.ws_col;
  return 80; /* fallback */
}

static void render(editor_state *s) {
  _render_len = 0;

  int term_rows = get_term_rows();
  int term_cols = get_term_cols();
  int content_rows = term_rows - 2; /* reserve bottom 2 lines for status */
  if (content_rows < 1) content_rows = 1;

  /* Clear screen and move cursor home */
  render_str("\033[2J\033[H");

  int row = 1, col = 1;
  int cursor_row = 1, cursor_col = 1;
  size_t text_offset = 0;
  int depth = 0;
  int in_inline = 0;
  int is_bold = 0;
  int cursor_found = 0;

  /* Selection range */
  size_t sel_start = s->cursor_pos < s->anchor_pos ? s->cursor_pos : s->anchor_pos;
  size_t sel_end   = s->cursor_pos > s->anchor_pos ? s->cursor_pos : s->anchor_pos;
  int has_sel = (sel_start != sel_end);
  int in_sel = 0;

  size_t total = rdoc_total_bytes(s->doc);
  if (total > 0) {
    rdoc_cursor c = rdoc_cursor_new(s->doc, 0);
    if (c.valid) {
      do {
        rdoc_token tok = rdoc_cursor_token(&c);

        switch (tok.kind) {
          case RDOC_TOKEN_TEXT: {
            for (size_t i = 0; i < tok.text_len; i++) {
              /* Save cursor screen position before this character */
              if (!cursor_found && text_offset == s->cursor_pos) {
                if (col == 1 && depth > 0)
                  render_depth_prefix(depth, &col);
                cursor_row = row;
                cursor_col = col;
                cursor_found = 1;
              }

              /* Update selection highlight state */
              if (has_sel) {
                int want = (text_offset >= sel_start && text_offset < sel_end);
                if (want != in_sel) {
                  in_sel = want;
                  if (row <= content_rows)
                    render_style(is_bold, in_inline, in_sel);
                }
              }

              if (tok.text_ptr[i] == '\n') {
                if (row <= content_rows) render_emit("\r\n", 2);
                row++;
                col = 1;
              } else {
                if (row <= content_rows) {
                  if (col == 1 && depth > 0)
                    render_depth_prefix(depth, &col);
                  render_emit((const char *)&tok.text_ptr[i], 1);
                }
                col++;
              }
              text_offset++;
            }
            /* Turn off selection at end of text run if needed */
            if (has_sel && in_sel &&
                text_offset >= sel_end) {
              in_sel = 0;
              if (row <= content_rows)
                render_style(is_bold, in_inline, in_sel);
            }
            /* Check cursor at end of this text token */
            if (!cursor_found && text_offset == s->cursor_pos) {
              if (col == 1 && depth > 0)
                render_depth_prefix(depth, &col);
              cursor_row = row;
              cursor_col = col;
              cursor_found = 1;
            }
            break;
          }

          case RDOC_TOKEN_MARK_CHANGE: {
            is_bold = (tok.mark_flags & 1) ? 1 : 0;
            if (row <= content_rows)
              render_style(is_bold, in_inline, in_sel);
            break;
          }

          case RDOC_TOKEN_BLOCK_OPEN: {
            if (col > 1) {
              if (row <= content_rows) render_emit("\r\n", 2);
              row++;
              col = 1;
            }
            if (row <= content_rows) {
              if (depth > 0) render_depth_prefix(depth, &col);
              /* ┌── */
              render_emit("\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80", 9);
              col += 3;
              render_emit("\r\n", 2);
            }
            row++;
            col = 1;
            depth++;
            /* Check if cursor should be placed inside this block */
            if (!cursor_found && text_offset == s->cursor_pos) {
              render_depth_prefix(depth, &col);
              cursor_row = row;
              cursor_col = col;
              cursor_found = 1;
            }
            break;
          }

          case RDOC_TOKEN_BLOCK_CLOSE: {
            depth--;
            if (col > 1) {
              if (row <= content_rows) render_emit("\r\n", 2);
              row++;
              col = 1;
            }
            if (row <= content_rows) {
              if (depth > 0) render_depth_prefix(depth, &col);
              /* └── */
              render_emit("\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80", 9);
              col += 3;
              render_emit("\r\n", 2);
            }
            row++;
            col = 1;
            break;
          }

          case RDOC_TOKEN_INLINE_OPEN: {
            if (row <= content_rows) {
              if (col == 1 && depth > 0)
                render_depth_prefix(depth, &col);
              in_inline = 1;
              render_style(is_bold, in_inline, in_sel);
              render_emit("\xc2\xab", 2); /* « */
            } else {
              in_inline = 1;
            }
            col++;
            break;
          }

          case RDOC_TOKEN_INLINE_CLOSE: {
            if (row <= content_rows) {
              render_emit("\xc2\xbb", 2); /* » */
              in_inline = 0;
              render_style(is_bold, in_inline, in_sel);
            } else {
              in_inline = 0;
            }
            col++;
            break;
          }
        }
      } while (rdoc_cursor_advance(&c));
      rdoc_cursor_free(c);
    }
  }

  /* Cursor at end or not yet placed */
  if (!cursor_found) {
    if (col == 1 && depth > 0) render_depth_prefix(depth, &col);
    cursor_row = row;
    cursor_col = col;
  }

  /* Reset style */
  render_str("\033[0m");

  /* ---- Status bar (2 lines at bottom) ---- */

  /* Line 1: keybindings */
  {
    char pos_buf[32];
    int len = snprintf(pos_buf, sizeof(pos_buf), "\033[%d;1H", term_rows - 1);
    render_emit(pos_buf, (size_t)len);
  }
  render_str("\033[7m"); /* reverse video */
  {
    const char *bar1 = " ^B Bold  ^K Block  ^I Inline  ^C Quit";
    render_str(bar1);
    /* Pad to full width */
    int written = (int)strlen(bar1);
    for (int i = written; i < term_cols; i++) render_emit(" ", 1);
  }

  /* Line 2: cursor info, marks, selection, char count */
  {
    char pos_buf[32];
    int len = snprintf(pos_buf, sizeof(pos_buf), "\033[%d;1H", term_rows);
    render_emit(pos_buf, (size_t)len);
  }
  render_str("\033[7m"); /* reverse video */
  {
    /* Compute line/col for status display */
    size_t ln, cl;
    cursor_to_line_col(s->doc, s->cursor_pos, &ln, &cl);

    /* Check mark state at cursor */
    size_t raw_pos = cursor_to_raw(s->doc, s->cursor_pos);
    rdoc_mark_result mr = rdoc_mark_at(s->doc, raw_pos);
    int cursor_bold = (mr.flags & 1) ? 1 : 0;

    /* Build status line */
    char bar2[256];
    int off = snprintf(bar2, sizeof(bar2), " Ln %zu, Col %zu", ln + 1, cl + 1);
    if (cursor_bold)
      off += snprintf(bar2 + off, sizeof(bar2) - (size_t)off, "  BOLD");
    if (has_sel) {
      size_t sel_size = sel_end - sel_start;
      off += snprintf(bar2 + off, sizeof(bar2) - (size_t)off, "  Sel: %zu chars", sel_size);
    }
    {
      size_t total_chars = rdoc_char_count(s->doc);
      off += snprintf(bar2 + off, sizeof(bar2) - (size_t)off, "  %zu chars", total_chars);
    }

    render_emit(bar2, (size_t)off);
    /* Pad to full width */
    for (int i = off; i < term_cols; i++) render_emit(" ", 1);
  }

  /* Reset style and position terminal cursor at edit point */
  render_str("\033[0m");
  /* Clamp cursor to content area */
  if (cursor_row > content_rows) cursor_row = content_rows;
  {
    char pos_buf[32];
    int len = snprintf(pos_buf, sizeof(pos_buf), "\033[%d;%dH", cursor_row, cursor_col);
    render_emit(pos_buf, (size_t)len);
  }

  render_flush();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void) {
  setup_signals();
  enable_raw_mode();

  /* Initialize editor state */
  editor_state state;
  state.doc = rdoc_new();
  state.cursor_pos = 0;
  state.anchor_pos = 0;
  state.next_id = 1;
  state.cursor_raw_hint = 0;

  /* Initial render */
  render(&state);

  /* Main loop: read_key -> handle_key -> render */
  for (;;) {
    key_event ev = read_key();
    if (ev.kind == KEY_CTRL_C) {
      break;
    }
    handle_key(&state, ev);
    render(&state);
  }

  /* Cleanup */
  rdoc_unref(state.doc);
  disable_raw_mode();
  write(STDOUT_FILENO, "\033[2J\033[H", 7);

  return 0;
}
