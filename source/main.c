/* ZType for Wii - typing shooter for USB keyboards. Platform glue: video, input, timing. */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <string.h>
#include "gfx.h"
#include "game.h"
#include "sound.h"
#include "Roboto_Light_ttf.h"
#include "Roboto_Regular_ttf.h"
#include "Roboto_Medium_ttf.h"

static void *xfb[2];
static int fbi = 0;
static GXRModeObj *rmode;
static u32 yuvrow[SW / 2];

static inline u32 yuvpair(u32 a, u32 b) {
	int r0 = (a >> 16) & 255, g0 = (a >> 8) & 255, b0 = a & 255;
	int r1 = (b >> 16) & 255, g1 = (b >> 8) & 255, b1 = b & 255;
	int y0 = 16 + ((66 * r0 + 129 * g0 + 25 * b0) >> 8);
	int y1 = 16 + ((66 * r1 + 129 * g1 + 25 * b1) >> 8);
	int r = (r0 + r1) >> 1, g = (g0 + g1) >> 1, bl = (b0 + b1) >> 1;
	int cb = 128 + ((-38 * r - 74 * g + 112 * bl) >> 8);
	int cr = 128 + ((112 * r - 94 * g - 18 * bl) >> 8);
	return (y0 << 24) | (cb << 16) | (y1 << 8) | cr;
}

static void present(void) {
	int H2 = rmode->xfbHeight, W2 = rmode->fbWidth / 2;
	u32 *dst = xfb[fbi];
	for (int y = 0, last = -1; y < H2; y++) {
		int sy = y * SH / H2;
		if (sy != last) {
			last = sy;
			const u32 *src = fb[sy];
			for (int x = 0; x < SW / 2; x++) yuvrow[x] = yuvpair(src[x * 2], src[x * 2 + 1]);
		}
		u32 *row = dst + y * W2;
		if (W2 == SW / 2) memcpy(row, yuvrow, W2 * 4);
		else for (int x = 0; x < W2; x++) row[x] = yuvrow[x * (SW / 2) / W2];
	}
	VIDEO_SetNextFramebuffer(dst);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	fbi ^= 1;
}

int main(void) {
	VIDEO_Init();
	WPAD_Init();
	KEYBOARD_Init(NULL);
	sound_init();
	int sdOK = fatInitDefault();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_Configure(rmode);
	VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

	gfx_init(Roboto_Light_ttf, Roboto_Regular_ttf, Roboto_Medium_ttf);
	game_init(sdOK);

	u64 last = gettime();
	int kbOn = 0;
	while (!game_wants_quit()) {
		WPAD_ScanPads();
		u32 d = WPAD_ButtonsDown(0);
		if (d & WPAD_BUTTON_HOME) break;
		if (d & WPAD_BUTTON_UP) game_key(K_UP);
		if (d & WPAD_BUTTON_DOWN) game_key(K_DOWN);
		if (d & WPAD_BUTTON_LEFT) game_key(K_LEFT);
		if (d & WPAD_BUTTON_RIGHT) game_key(K_RIGHT);
		if (d & WPAD_BUTTON_A) game_key(K_OK);
		if (d & WPAD_BUTTON_PLUS) game_key(K_MENU);
		if (d & WPAD_BUTTON_1) game_key(K_BACK);
		if (d & WPAD_BUTTON_2) game_key(K_ABOUT);

		keyboard_event ke;
		while (KEYBOARD_GetEvent(&ke)) {
			if (ke.type == KEYBOARD_CONNECTED) kbOn = 1;
			else if (ke.type == KEYBOARD_DISCONNECTED) kbOn = 0;
			else if (ke.type == KEYBOARD_PRESSED) {
				kbOn = 1;
				switch (ke.symbol) {
				case KS_Return: game_key(K_OK); break;
				case KS_Escape: game_key(K_MENU); break;
				case KS_BackSpace: case KS_Delete: game_key(K_BACK); break;
				case KS_Up: game_key(K_UP); break;
				case KS_Down: game_key(K_DOWN); break;
				case KS_Left: game_key(K_LEFT); break;
				case KS_Right: game_key(K_RIGHT); break;
				default: if (ke.symbol > 32 && ke.symbol < 127) game_char((char)ke.symbol); break;
				}
			}
		}
		game_set_keyboard(kbOn);

		u64 now = gettime();
		float dt = (float)ticks_to_microsecs(now - last) / 1e6f;
		last = now;
		if (dt > .05f) dt = .05f;
		if (dt < .001f) dt = .001f;
		game_update(dt);
		game_draw();
		present();
	}
	KEYBOARD_Deinit();
	return 0;
}
