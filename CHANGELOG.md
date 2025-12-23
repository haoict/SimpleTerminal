# Changelog

All notable changes to SimpleTerminal will be documented in this file.

## [2.0.0] - 2025-12-23

### Major Changes

#### SDL2 Migration
- **Migrated from SDL 1.2 to SDL 2**: Complete rewrite of the rendering pipeline and event handling system for better performance and compatibility with modern systems
- Updated window management and texture rendering to use SDL2 APIs
- Improved renderer architecture with hardware acceleration support

#### Font System Overhaul
- **5 Embedded Bitmap Fonts**: Added multiple embedded font options (fonts 1-5) with different sizes and styles
  - Font 1: 6x6 pixels (default)
  - Font 2: 8x8 pixels
  - Font 3: 3x5 pixels (PICO-8 style, compact)
  - Font 4: 8x10 pixels (larger)
  - Font 5: 8x10 pixels (alternative style)
- **TTF Font Support**: Added support for TrueType fonts via SDL_ttf
  - Custom font loading with `-font /path/to/font.ttf`
  - Adjustable font size with `-fontsize N`
  - Font rendering modes with `-fontshade 0|1|2` (solid, blended, shaded)
- Web-based embedded font editor available at https://simple-terminal-psi.vercel.app

#### Display Rotation
- **Screen Rotation Support**: Added `-rotate` option to rotate terminal display
  - Supports 0°, 90°, 180°, and 270° rotation
  - Useful for handheld devices with different screen orientations
  - Content and on-screen keyboard rotate together

#### Scrollback Buffer
- **Scrollback History**: Navigate through terminal history
  - Configurable buffer size (default: 256 lines)
  - Scroll up/down with dedicated keys
  - Visual indicator showing scroll position `[N]^` in top-right corner
  - Auto-reset on new input
  - Separate scrollback for primary and alternate screens

### Additional Improvements
- Enhanced keyboard handling with better modifier key support
- Improved VT100 escape sequence compatibility
- Better cursor save/restore behavior for alternate screen modes
- Fixed screen switching issues (modes 47, 1047, 1048, 1049)
- Screenshot functionality with timestamp-based filenames
- Window resizing support for non-embedded builds

### Command Line Options
New options added:
- `-rotate [0|90|180|270]`: Rotate display content
- `-font [1|2|3|4|5|/path/to/font.ttf]`: Select font
- `-fontsize N`: Set TTF font size (default: 12)
- `-fontshade [0|1|2]`: Set TTF rendering quality (0=solid, 1=blended, 2=shaded)
- `-useEmbeddedFontForKeyboard [0|1]`: Force bitmap font for on-screen keyboard

### Breaking Changes
- SDL 1.2 is no longer supported; SDL 2.0+ is now required
- Build system updated to link against SDL2 libraries
- Some deprecated SDL 1.2 APIs removed

### Bug Fixes
- Fixed cursor positioning issues after screen swaps
- Improved terminal resize handling
- Better cleanup on application exit
- Fixed memory leaks in font rendering

---

## [1.x] - Previous Versions
See git history for changes in earlier versions.

