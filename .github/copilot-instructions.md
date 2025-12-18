# SimpleTerminal - AI Coding Assistant Instructions

## Project Overview
SimpleTerminal is an SDL2-based VT100 terminal emulator for embedded Linux handhelds, migrated from SDL1.2. It runs on devices like RGB30, H700, and Raspberry Pi, as well as generic Linux PCs.

## Architecture

### Core Components
- **[src/main.c](../src/main.c)**: SDL2 event loop, rendering pipeline, window management, threading coordination
- **[src/vt100.c](../src/vt100.c)**: VT100 escape sequence parser, terminal state machine, PTY management
- **[src/keyboard.c](../src/keyboard.c)**: On-screen keyboard for handhelds, joystick input mapping
- **[src/font.c](../src/font.c)**: Dual font system - embedded bitmap fonts + TTF rendering via SDL_ttf
- **[src/config.h](../src/config.h)**: Runtime configuration (colors, defaults, dimensions)

### Threading Model
- **Main thread**: SDL event loop, rendering (draws terminal + on-screen keyboard)
- **TTY thread** (`tty_thread` in main.c): Reads PTY, processes VT100 sequences, updates terminal state
- Coordination: `thread_should_exit` volatile flag for clean shutdown
- Critical: TTY thread runs independently; main thread polls for redraws

### Data Flow
```
User Input → SDL Events → keyboard mapping → PTY write → shell
PTY read → VT100 parser → terminal buffer → redraw flag → SDL render
```

### Rotation System
The `-rotate` flag rotates content *inside* the window (not the window itself):
- Content buffer dimensions swap for 90°/270° (compose_w/compose_h swapped)
- Characters and on-screen keyboard rotate together
- Window size stays fixed
- See `scale_to_size()` in main.c for surface recreation logic

## Build System

### Cross-Compilation
```bash
# Set CROSS_COMPILE prefix for buildroot toolchains
export CROSS_COMPILE=/path/to/toolchain/bin/aarch64-none-linux-gnu-
make PLATFORM=rgb30  # or h700, pi
```

### Platform Defines
- `BR2`: Buildroot-based embedded builds (disables window resize, enables joystick mappings)
- `RGB30`, `H700`, `RPI`: Device-specific quirks (e.g., RGB30 has jbutton10 startup workaround)
- Check [keyboard.h](../src/keyboard.h) for conditional joystick button mappings vs PC keyboard

### Dependencies
- `libsdl2-dev`, `libsdl2-ttf-dev` required
- `-lutil` for PTY functions (forkpty)
- `-lpthread` for threading

## Code Conventions

### Font System
Two rendering paths coexist:
1. **Embedded bitmap fonts** (font 1-5): Fixed-size, hardcoded in font.c, used as fallback
2. **TTF fonts**: Dynamic sizing via `-font /path/to.ttf -fontsize N -fontshade 0|1|2`
- Always check `is_ttf_loaded()` before calling TTF functions
- Keyboard can force bitmap font with `opt_use_embedded_font_for_keyboard`

### Color Management
- 256-color palette in `colormap[]` (config.h)
- Indexes 0-15: standard ANSI colors
- Indexes 256+: custom UI colors (cursor, background variants)
- Use `drawing_ctx.colors[]` array after `init_color_map()` call

### VT100 Escape Sequences
- Parser state in `term` global (vt100.c)
- Minimalist philosophy from LEGACY file: "only support what is really needed"
- Missing a sequence? Add to `csiparse()` or `strparse()` functions, test with real terminal apps

### Keyboard Input
- **Handhelds**: Joystick buttons mapped in keyboard.h (e.g., `KEY_OSKACTIVATE = JOYBUTTON_X`)
- **PC**: Standard SDL keyboard events
- On-screen keyboard renders at `location` (top/bottom) with 6-row layout in keyboard.c

## Development Workflows

### Running Locally
```bash
make
./simple-terminal
./simple-terminal -font 2  # test embedded font
./simple-terminal -scale 1 -font /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf -fontsize 12
./simple-terminal -rotate 270  # test rotation
./simple-terminal -r "ls -la"  # auto-run commands
```

### Debugging Tips
- Use `fprintf(stderr, ...)` - stdout is captured by PTY
- Check `term.dirty[]` flags to understand redraw triggers
- Monitor `TIMEDIFF` macro for frame timing issues
- For handheld builds, test without `-DBR2` first on PC

### Embedded Font Editing
Web-based tool at [https://simple-terminal-psi.vercel.app](https://simple-terminal-psi.vercel.app) (source in [embedded-font-editor/](../embedded-font-editor/))
- Edit font.c bitmap arrays directly
- Font dimensions fixed at compile time

## Critical Patterns

### Surface Management
Three surfaces compose final frame:
1. `main_window.surface`: Terminal content (may be rotated dimensions)
2. `osk_screen`: Composited terminal + on-screen keyboard
3. `rotated_screen`: Final rotated output matching window size

All freed/recreated on resize in `scale_to_size()`.

### PTY Communication
- `cmdfd`: PTY file descriptor for shell I/O
- `tty_write()` for sending keystrokes (main.c)
- `tty_read()` in TTY thread processes escape sequences (vt100.c)
- Clean shutdown requires unblocking TTY thread (send dummy char, disable mouse tracking)

### Non-Printing Keys
Array `non_printing_keyboard_keys[]` maps SDL keycodes to escape sequences (e.g., arrow keys → `\033[A`)
- Use `XK_ANY_MOD` for keys without modifier requirements
- Modifiers: `KMOD_LALT`, `KMOD_CTRL`, `KMOD_LSHIFT`/`KMOD_RSHIFT`

## Common Tasks

**Adding a command-line option**: 
1. Add to USAGE string in main.c
2. Parse in `main()` (look for existing `-flag` pattern)
3. Store in `opt_*` static variable
4. Apply in relevant init function

**Supporting a new escape sequence**:
1. Add case to `csiparse()` in vt100.c
2. Implement handler function (e.g., `t_deletechar()`)
3. Test with `echo -e '\033[sequence'` or real app (vim, htop)

**Fixing handheld button mapping**:
1. Check joystick button IDs in keyboard.h `#if defined(BR2)` section
2. Update `KEY_*` defines or `joybutton_convert()` in keyboard.c
3. Test on device (PC builds won't use joystick mappings)
