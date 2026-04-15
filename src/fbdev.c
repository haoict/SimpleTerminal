#include "fbdev.h"

static int fb_fd = -1;
static void *fb_mem = NULL;
static size_t fb_mem_size = 0;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static bool fb_initialized = false;
static struct termios orig_termios;
static bool stdin_raw = false;

#define EVENT_QUEUE_SIZE 64
static SDL_Event event_queue[EVENT_QUEUE_SIZE];
static int event_queue_head = 0;
static int event_queue_tail = 0;

static void push_internal_event(const SDL_Event *event) {
    int next = (event_queue_tail + 1) % EVENT_QUEUE_SIZE;
    if (next == event_queue_head) return;
    event_queue[event_queue_tail] = *event;
    event_queue_tail = next;
}

static bool pop_internal_event(SDL_Event *event) {
    if (event_queue_head == event_queue_tail) return false;
    *event = event_queue[event_queue_head];
    event_queue_head = (event_queue_head + 1) % EVENT_QUEUE_SIZE;
    return true;
}

static int setup_raw_stdin(void) {
    if (!isatty(STDIN_FILENO)) return 0;

    if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) return -1;
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return -1;
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0) return -1;
    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    stdin_raw = true;
    return 0;
}

static void restore_raw_stdin(void) {
    if (stdin_raw) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        stdin_raw = false;
    }
}

static int parse_input_escape(const char *buf, int len, SDL_Event *event) {
    if (len >= 3 && buf[0] == 0x1b && buf[1] == '[') {
        switch (buf[2]) {
        case 'A':
            event->type = SDL_KEYDOWN;
            event->key.type = SDL_KEYDOWN;
            event->key.state = SDL_PRESSED;
            event->key.keysym.sym = 0x40000052; // SDLK_UP
            event->key.keysym.mod = KMOD_NONE;
            return 3;
        case 'B':
            event->type = SDL_KEYDOWN;
            event->key.type = SDL_KEYDOWN;
            event->key.state = SDL_PRESSED;
            event->key.keysym.sym = 0x40000051; // SDLK_DOWN
            event->key.keysym.mod = KMOD_NONE;
            return 3;
        case 'C':
            event->type = SDL_KEYDOWN;
            event->key.type = SDL_KEYDOWN;
            event->key.state = SDL_PRESSED;
            event->key.keysym.sym = 0x4000004f; // SDLK_RIGHT
            event->key.keysym.mod = KMOD_NONE;
            return 3;
        case 'D':
            event->type = SDL_KEYDOWN;
            event->key.type = SDL_KEYDOWN;
            event->key.state = SDL_PRESSED;
            event->key.keysym.sym = 0x40000050; // SDLK_LEFT
            event->key.keysym.mod = KMOD_NONE;
            return 3;
        case 'H':
            event->type = SDL_KEYDOWN;
            event->key.keysym.sym = 0x4000004a; // SDLK_HOME
            event->key.keysym.mod = KMOD_NONE;
            event->key.state = SDL_PRESSED;
            return 3;
        case 'F':
            event->type = SDL_KEYDOWN;
            event->key.keysym.sym = 0x4000004d; // SDLK_END
            event->key.keysym.mod = KMOD_NONE;
            event->key.state = SDL_PRESSED;
            return 3;
        case '2':
            if (len >= 4 && buf[3] == '~') {
                event->type = SDL_KEYDOWN;
                event->key.type = SDL_KEYDOWN;
                event->key.state = SDL_PRESSED;
                event->key.keysym.sym = 0x4000002d; // SDLK_INSERT
                event->key.keysym.mod = KMOD_NONE;
                return 4;
            }
            break;
        case '3':
            if (len >= 4 && buf[3] == '~') {
                event->type = SDL_KEYDOWN;
                event->key.type = SDL_KEYDOWN;
                event->key.state = SDL_PRESSED;
                event->key.keysym.sym = 0x4000002e; // SDLK_DELETE
                event->key.keysym.mod = KMOD_NONE;
                return 4;
            }
            break;
        case '5':
            if (len >= 4 && buf[3] == '~') {
                event->type = SDL_KEYDOWN;
                event->key.type = SDL_KEYDOWN;
                event->key.state = SDL_PRESSED;
                event->key.keysym.sym = 0x4000004b; // SDLK_PAGEUP
                event->key.keysym.mod = KMOD_NONE;
                return 4;
            }
            break;
        case '6':
            if (len >= 4 && buf[3] == '~') {
                event->type = SDL_KEYDOWN;
                event->key.type = SDL_KEYDOWN;
                event->key.state = SDL_PRESSED;
                event->key.keysym.sym = 0x4000004e; // SDLK_PAGEDOWN
                event->key.keysym.mod = KMOD_NONE;
                return 4;
            }
            break;
        }
    }
    return 0;
}

static void pump_stdin_events(void) {
    if (!stdin_raw) setup_raw_stdin();
    char buf[32];
    ssize_t len = read(STDIN_FILENO, buf, sizeof(buf));
    if (len <= 0) return;

    int pos = 0;
    while (pos < len) {
        SDL_Event ev;
        memset(&ev, 0, sizeof(ev));
        if (buf[pos] == 0x1b) {
            int consumed = parse_input_escape(&buf[pos], len - pos, &ev);
            if (consumed > 0) {
                push_internal_event(&ev);
                pos += consumed;
                continue;
            }
        }

        char ch = buf[pos++];
        if (ch == '\r' || ch == '\n') {
            ev.type = SDL_KEYDOWN;
            ev.key.type = SDL_KEYDOWN;
            ev.key.state = SDL_PRESSED;
            ev.key.keysym.sym = '\r';
            ev.key.keysym.mod = KMOD_NONE;
            push_internal_event(&ev);
            continue;
        }
        if (ch == 0x7f || ch == 0x08) {
            ev.type = SDL_KEYDOWN;
            ev.key.type = SDL_KEYDOWN;
            ev.key.state = SDL_PRESSED;
            ev.key.keysym.sym = 0x7f;
            ev.key.keysym.mod = KMOD_NONE;
            push_internal_event(&ev);
            continue;
        }

        if ((unsigned char)ch < 32 && ch != '\t') {
            SDL_Event tev;
            memset(&tev, 0, sizeof(tev));
            tev.type = SDL_TEXTINPUT;
            tev.text.type = SDL_TEXTINPUT;
            tev.text.text[0] = ch;
            tev.text.text[1] = '\0';
            push_internal_event(&tev);
            continue;
        }

        SDL_Event tev;
        memset(&tev, 0, sizeof(tev));
        tev.type = SDL_TEXTINPUT;
        tev.text.type = SDL_TEXTINPUT;
        tev.text.text[0] = ch;
        tev.text.text[1] = '\0';
        push_internal_event(&tev);
    }
}

int SDL_Init(Uint32 flags) {
    (void)flags;
    fb_initialized = true;
    return 0;
}

void SDL_Quit(void) {
    restore_raw_stdin();
    if (fb_mem) {
        munmap(fb_mem, fb_mem_size);
        fb_mem = NULL;
    }
    if (fb_fd >= 0) {
        close(fb_fd);
        fb_fd = -1;
    }
    fb_initialized = false;
}

static inline Uint32 fb_convert_component(Uint8 value, int length) {
    if (length <= 0) return 0;
    if (length >= 8) return (Uint32)value >> (8 - length);
    return (Uint32)((value * ((1u << length) - 1) + 127) / 255);
}

static Uint32 fb_pack_pixel(Uint8 r, Uint8 g, Uint8 b) {
    Uint32 pixel = 0;
    pixel |= fb_convert_component(r, vinfo.red.length) << vinfo.red.offset;
    pixel |= fb_convert_component(g, vinfo.green.length) << vinfo.green.offset;
    pixel |= fb_convert_component(b, vinfo.blue.length) << vinfo.blue.offset;
    if (vinfo.transp.length > 0) {
        pixel |= ((1u << vinfo.transp.length) - 1) << vinfo.transp.offset;
    }
    return pixel;
}

static int fbdev_open(void) {
    if (fb_fd >= 0 && fb_mem) return 0;
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) return -1;
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }
    if (vinfo.bits_per_pixel != 8 && vinfo.bits_per_pixel != 16 && vinfo.bits_per_pixel != 24 && vinfo.bits_per_pixel != 32) {
        close(fb_fd);
        fb_fd = -1;
        return -1;
    }
    fb_mem_size = finfo.smem_len;
    fb_mem = mmap(NULL, fb_mem_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_mem == MAP_FAILED) {
        close(fb_fd);
        fb_fd = -1;
        fb_mem = NULL;
        return -1;
    }
    return 0;
}

int fbdev_init(void) {
    if (fbdev_open() != 0) return -1;
    fb_initialized = true;
    return 0;
}

void fbdev_present(SDL_Surface *surface) {
    if (!surface) return;
    if (fbdev_open() != 0) return;

    int copy_w = surface->w;
    int copy_h = surface->h;
    if (copy_w > (int)vinfo.xres) copy_w = vinfo.xres;
    if (copy_h > (int)vinfo.yres) copy_h = vinfo.yres;

    for (int y = 0; y < copy_h; y++) {
        uint8_t *dst = (uint8_t *)fb_mem + (size_t)y * finfo.line_length;
        uint8_t *src = (uint8_t *)surface->pixels + (size_t)y * surface->pitch;

        for (int x = 0; x < copy_w; x++) {
            Uint16 src_pixel = *(Uint16 *)(src + x * 2);
            Uint8 r = (Uint8)(((src_pixel >> 11) & 0x1F) * 255 / 31);
            Uint8 g = (Uint8)(((src_pixel >> 5) & 0x3F) * 255 / 63);
            Uint8 b = (Uint8)((src_pixel & 0x1F) * 255 / 31);
            Uint32 fb_pixel = fb_pack_pixel(r, g, b);

            switch (vinfo.bits_per_pixel) {
            case 16:
                ((Uint16 *)dst)[x] = (Uint16)fb_pixel;
                break;
            case 24:
                dst[x * 3 + 0] = (Uint8)(fb_pixel & 0xFF);
                dst[x * 3 + 1] = (Uint8)((fb_pixel >> 8) & 0xFF);
                dst[x * 3 + 2] = (Uint8)((fb_pixel >> 16) & 0xFF);
                break;
            case 32:
                ((Uint32 *)dst)[x] = fb_pixel;
                break;
            case 8: {
                Uint8 gray = (Uint8)((299 * r + 587 * g + 114 * b + 500) / 1000);
                dst[x] = gray;
                break;
            }
            default:
                break;
            }
        }
    }
}

int SDL_WasInit(Uint32 flags) {
    (void)flags;
    return fb_initialized ? 1 : 0;
}

void SDL_ShowCursor(int state) {
    (void)state;
}

void SDL_StartTextInput(void) {
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth, Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask) {
    (void)flags;
    (void)Rmask;
    (void)Gmask;
    (void)Bmask;
    (void)Amask;
    if (depth != 16) return NULL;
    SDL_Surface *surface = calloc(1, sizeof(SDL_Surface));
    if (!surface) return NULL;
    surface->w = width;
    surface->h = height;
    surface->pitch = width * 2;
    surface->pixels = calloc(1, surface->pitch * height);
    if (!surface->pixels) {
        free(surface);
        return NULL;
    }
    SDL_PixelFormat *fmt = calloc(1, sizeof(SDL_PixelFormat));
    if (!fmt) {
        free(surface->pixels);
        free(surface);
        return NULL;
    }
    fmt->BytesPerPixel = 2;
    surface->format = fmt;
    return surface;
}

void SDL_FreeSurface(SDL_Surface *surface) {
    if (!surface) return;
    if (surface->pixels) free(surface->pixels);
    if (surface->format) free(surface->format);
    free(surface);
}

Uint32 SDL_MapRGB(SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b) {
    (void)format;
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

int SDL_FillRect(SDL_Surface *surface, const SDL_Rect *rect, Uint32 color) {
    if (!surface) return -1;
    SDL_Rect local = {0, 0, surface->w, surface->h};
    if (rect) local = *rect;
    if (local.x < 0) local.x = 0;
    if (local.y < 0) local.y = 0;
    if (local.x + local.w > surface->w) local.w = surface->w - local.x;
    if (local.y + local.h > surface->h) local.h = surface->h - local.y;
    for (int y = local.y; y < local.y + local.h; y++) {
        Uint16 *row = (Uint16 *)((uint8_t *)surface->pixels + y * surface->pitch) + local.x;
        for (int x = 0; x < local.w; x++) row[x] = (Uint16)color;
    }
    return 0;
}

int SDL_LockSurface(SDL_Surface *surface) {
    (void)surface;
    return 0;
}

void SDL_UnlockSurface(SDL_Surface *surface) {
    (void)surface;
}

int SDL_SaveBMP(SDL_Surface *surface, const char *file) {
    if (!surface || !file) return -1;
    FILE *fp = fopen(file, "wb");
    if (!fp) return -1;
    int width = surface->w;
    int height = surface->h;
    int row_bytes = width * 2;
    int padded = (row_bytes + 3) & ~3;
    int filesize = 14 + 40 + padded * height;
    unsigned char header[14] = { 'B', 'M' };
    header[2] = filesize & 0xff;
    header[3] = (filesize >> 8) & 0xff;
    header[4] = (filesize >> 16) & 0xff;
    header[5] = (filesize >> 24) & 0xff;
    header[10] = 54;
    fwrite(header, 1, 14, fp);
    unsigned char dib[40] = {0};
    dib[0] = 40;
    dib[4] = width & 0xff;
    dib[5] = (width >> 8) & 0xff;
    dib[6] = (width >> 16) & 0xff;
    dib[7] = (width >> 24) & 0xff;
    dib[8] = height & 0xff;
    dib[9] = (height >> 8) & 0xff;
    dib[10] = (height >> 16) & 0xff;
    dib[11] = (height >> 24) & 0xff;
    dib[12] = 1;
    dib[14] = 16;
    dib[16] = 3;
    dib[20] = padded * height & 0xff;
    dib[21] = (padded * height >> 8) & 0xff;
    dib[22] = (padded * height >> 16) & 0xff;
    dib[23] = (padded * height >> 24) & 0xff;
    fwrite(dib, 1, 40, fp);
    unsigned char *row = calloc(1, padded);
    if (!row) {
        fclose(fp);
        return -1;
    }
    for (int y = height - 1; y >= 0; y--) {
        memcpy(row, (uint8_t *)surface->pixels + y * surface->pitch, row_bytes);
        fwrite(row, 1, padded, fp);
    }
    free(row);
    fclose(fp);
    return 0;
}

static Uint32 get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000u + tv.tv_usec / 1000u;
}

Uint32 SDL_GetTicks(void) {
    return get_time_ms();
}

void SDL_Delay(Uint32 ms) {
    usleep(ms * 1000);
}

Uint8 *SDL_GetError(void) {
    static char err[128];
    snprintf(err, sizeof(err), "fbdev backend only");
    return (Uint8 *)err;
}

static void *timer_thread(void *arg) {
    struct { Uint32 interval; SDL_TimerCallback callback; void *param; } *ctx = arg;
    usleep(ctx->interval * 1000u);
    ctx->callback(ctx->interval, ctx->param);
    free(ctx);
    return NULL;
}

int SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param) {
    if (!callback) return -1;
    pthread_t thread;
    struct { Uint32 interval; SDL_TimerCallback callback; void *param; } *ctx = malloc(sizeof(*ctx));
    if (!ctx) return -1;
    ctx->interval = interval;
    ctx->callback = callback;
    ctx->param = param;
    if (pthread_create(&thread, NULL, timer_thread, ctx) != 0) {
        free(ctx);
        return -1;
    }
    pthread_detach(thread);
    return 0;
}

int SDL_PollEvent(SDL_Event *event) {
    pump_stdin_events();
    if (pop_internal_event(event)) return 1;
    return 0;
}

int SDL_PushEvent(SDL_Event *event) {
    if (!event) return -1;
    push_internal_event(event);
    return 1;
}

SDL_Thread *SDL_CreateThread(int (*fn)(void *), const char *name, void *data) {
    (void)name;
    SDL_Thread *thread = malloc(sizeof(SDL_Thread));
    if (!thread) return NULL;
    if (pthread_create(thread, NULL, (void *(*)(void *))fn, data) != 0) {
        free(thread);
        return NULL;
    }
    return thread;
}

int SDL_WaitThread(SDL_Thread *thread, int *status) {
    if (!thread) return -1;
    void *retval;
    int rc = pthread_join(*thread, &retval);
    if (status) *status = rc;
    free(thread);
    return rc;
}

const char *SDL_GetKeyName(SDL_Keycode key) {
    static char name[32];
    if (key > 0 && key < 128) {
        name[0] = (char)key;
        name[1] = '\0';
        return name;
    }
    switch (key) {
    case 0x40000052: return "Up";
    case 0x40000051: return "Down";
    case 0x40000050: return "Left";
    case 0x4000004f: return "Right";
    case 0x4000004a: return "Home";
    case 0x4000004d: return "End";
    case 0x4000002d: return "Insert";
    case 0x4000002e: return "Delete";
    case 0x4000004b: return "PageUp";
    case 0x4000004e: return "PageDown";
    default:
        snprintf(name, sizeof(name), "Key%d", key);
        return name;
    }
}

void SDL_SetModState(SDL_Keymod mod) {
    (void)mod;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
    (void)title;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)flags;
    return NULL;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags) {
    (void)window;
    (void)index;
    (void)flags;
    return NULL;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format, int access, int w, int h) {
    (void)renderer;
    (void)format;
    (void)access;
    (void)w;
    (void)h;
    return NULL;
}

void SDL_DestroyTexture(SDL_Texture *texture) {
    (void)texture;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer) {
    (void)renderer;
}

void SDL_DestroyWindow(SDL_Window *window) {
    (void)window;
}

SDL_Joystick *SDL_JoystickOpen(int device_index) {
    (void)device_index;
    return NULL;
}

void SDL_JoystickClose(SDL_Joystick *joystick) {
    (void)joystick;
}
