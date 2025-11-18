/**
 * https://github.com/yohanes/rg35xx-stock-sdl-demo
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <SDL/SDL.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <dirent.h>
#include <pthread.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <linux/input.h>

#define MENU_ITEMS 3
#define MENU_ITEM_HEIGHT 40
#define MENU_ITEM_SPACING 10

int gpio_keys_polled_fd;
int extra_keys_polled_fd;
pthread_t adc_thread;

#define RAW_UP 103
#define RAW_DOWN 108
#define RAW_LEFT 105
#define RAW_RIGHT 106
// #define RAW_A 304
// #define RAW_B 305
// #define RAW_X 307
// #define RAW_Y 306
// #define RAW_START 311
// #define RAW_SELECT 310
// #define RAW_MENU 312
// #define RAW_L1 308
// #define RAW_L2 314
// #define RAW_L3 313
// #define RAW_R1 309
// #define RAW_R2 315
// #define RAW_R3 316
// #define RAW_PLUS 115
// #define RAW_MINUS 114
// #define RAW_POWER 116
#define RAW_YAXIS 17
#define RAW_XAXIS 16

void open_gpio_keys_polled()
{
	DIR *dir;			// r4
	const char *d_name; // r7
	int i;				// r5
	int fd;				// r0
	int tmp;			// r6
	struct dirent *d;	// r0
	char v6[128];		// [sp+0h] [bp-298h] BYREF
	char s[536];		// [sp+80h] [bp-218h] BYREF

	dir = opendir("/dev/input");
	if (dir)
	{
		i = 7;
		while (1)
		{
			d = readdir(dir);
			if (!d)
				break;
			d_name = d->d_name;
			if (!strncmp(d->d_name, "event", 5u))
			{
				if (!--i)
					break;
				sprintf(s, "/dev/input/%s", d_name);
				fd = open(s, 0);
				tmp = fd;
				if (fd > 0 && ioctl(fd, EVIOCGNAME(128) /*0x80804506*/, v6) >= 0)
				{
#if defined(RG35XXPLUS)
					if (!strcmp(v6, "Deeplay-keys") || !strcmp(v6, "ANBERNIC-keys"))
#elif defined(H700_SDL12COMPAT)
					if (!strcmp(v6, "H700 Gamepad"))
#elif defined(R36S_SDL12COMPAT)
					if (!strcmp(v6, "GO-Super Gamepad"))
#elif defined(RGB30_SDL12COMPAT)
					if (!strcmp(v6, "retrogame_joypad"))
#elif defined(TRIMUISP)
					if (!strcmp(v6, "TRIMUI Player1"))
#endif
					{
						printf("open_gpio_keys_polled success: /dev/input/%s\n", d_name);
						gpio_keys_polled_fd = tmp;
						break;
					}

					close(tmp);
				}
			}
		}
		closedir(dir);
	}
}


int keep_going = 1;
int input_var_xx = 0;
int key_state;
int input_var_9 = 0;
int input_var_10 = 0;
int input_var_17 = 0;

void process_events(int ev_code, int ev_value, int ev_type)
{
	int scancode = ev_code;
	int sym = ev_code;
	if (ev_type != 1 && ev_type != 3)
	{
		return;
	}
#if defined(SDL12COMPAT)
	// ignore joysticks
	if (ev_code == 0 || ev_code == 1 || ev_code == 3 || ev_code == 4)
	{
		return;
	}
#endif
	// printf("process_events: ev_code=%d, ev_value=%d, ev_type=%d\n", ev_code, ev_value, ev_type);


	// DPAD: -1=no change,1=pressed,0=released
	if (ev_code == RAW_YAXIS)
	{
		if (ev_value == -1)
		{
			scancode = RAW_UP;
			sym = RAW_UP;
		}
		if (ev_value == 1)
		{
			scancode = RAW_DOWN;
			sym = RAW_DOWN;
		}
	}
	else if (ev_code == RAW_XAXIS)
	{
		if (ev_value == -1)
		{
			scancode = RAW_LEFT;
			sym = RAW_LEFT;
		}
		if (ev_value == 1)
		{
			scancode = RAW_RIGHT;
			sym = RAW_RIGHT;
		}
	}
	else
	{
		// A/B/X/Y
		// MENU/SELECT/START
		// L1/L2/R1/R2
	}

	SDL_Event sdl_event = {
		.key = {
			.type = (ev_value == 1 || ev_value == -1) ? SDL_KEYDOWN : SDL_KEYUP,
			.state = (ev_value == 1 || ev_value == -1) ? SDL_PRESSED : SDL_RELEASED,
			.keysym = {
				.scancode = scancode,
				.sym = sym,
				.mod = 0,
				.unicode = 0,
			}}};

	SDL_PushEvent(&sdl_event);

	return;
}

void *read_adc2key_thread(void *dummy)
{
	struct input_event input_ev;
	struct pollfd polldata[2];
	ssize_t rd;
	int ret;

	while (1)
	{
		if (!keep_going)
		{
			return dummy;
		}
		memset(polldata, 0, 0x10);
		if (0 < gpio_keys_polled_fd)
		{
			polldata[0].fd = gpio_keys_polled_fd;
			polldata[0].events = POLLIN;
		}
		if (extra_keys_polled_fd < 1)
		{
			if (0 >= gpio_keys_polled_fd)
			{
				printf("--<%s>-- not find event\n", "read_adc2key_thread");
				return dummy;
			}
		}
		else
		{
			polldata[1].fd = extra_keys_polled_fd;
			polldata[1].events = POLLIN;
		}
		ret = poll(polldata, 2, 300);
		if (ret < 0)
		{
			printf("<%s> ctrl+c is pressed ret=%d\n", "read_adc2key_thread", ret);
			keep_going = 0;
			input_var_xx = 0x100000;
			return dummy;
		}
		for (int i = 0; i < 2; i++)
		{
			if ((polldata[i].revents & POLLIN) != 0)
			{
				if (i == 0)
				{
					// gpio_keys_polled_fd;
					rd = read(polldata[0].fd, &input_ev, sizeof(input_ev));
					if (rd < 0)
					{
						printf("read keypad error ");
					}
					else
					{
						process_events(input_ev.code, input_ev.value, input_ev.type);
					}
				}
				else
				{
					// extra_keys_polled_fd;
					rd = read(polldata[1].fd, &input_ev, sizeof(input_ev));
					if (rd < 0)
					{
						printf("read extra_keys_polled_fd error ");
					}
					else
					{
						process_events(input_ev.code, input_ev.value, input_ev.type);
						// handle various power shortcuts here
						//  if (input_ev.type == 1) {
						//    if (input_ev.value == 0) {
						//      remove_timer();
						//      if (DAT_0002b650 != 0) {
						//        cpu_and_brightness(1);
						//      }
						//    }
						//    else {
						//      if (input_ev.value != 1) goto LAB_0000b7ea;
						//      add_timer();
						//    }
						//    set_sdl_tick();
						//  }
						// }
					}
				}
			}
		}
	}
}

int open_adc_bnt_input()
{
	open_gpio_keys_polled();
	if (gpio_keys_polled_fd <= 0)
	{
		printf("open_gpio_keys_polled error\n");  // rg35xxplus: /dev/input/event1, r36s: /dev/input/event2, trimuisp: /dev/input/event2
		return -1;
	}
#if defined(RGB30_SDL12COMPAT)
	extra_keys_polled_fd = open("/dev/input/event1", 0);
#elif defined(H700_SDL12COMPAT)
	extra_keys_polled_fd = open("/dev/input/event2", 0);
#else
	extra_keys_polled_fd = open("/dev/input/event0", 0);
#endif
	if (extra_keys_polled_fd < 0)
	{
		printf("Open %s error\n", "/dev/input/event0");
		close(gpio_keys_polled_fd);
		return -1;
	}
	if (pthread_create(&adc_thread, 0, read_adc2key_thread, 0))
	{
		printf("pthread_create error\n");
		return -1;
	}
	else
	{
		printf("keymon started\n");
		return 0;
	}
}
