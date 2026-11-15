#define _GNU_SOURCE
#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "boot-styles.h"

static uint32_t channel(uint8_t value, struct fb_bitfield field)
{
	if (!field.length)
		return 0;
	uint32_t max = (1U << field.length) - 1U;
	return ((uint32_t)value * max / 255U) << field.offset;
}

static void present(uint8_t *fb, size_t fb_size, struct fb_fix_screeninfo *fix,
		    struct fb_var_screeninfo *var, uint32_t *pixels)
{
	const unsigned bytes = var->bits_per_pixel / 8;
	for (uint32_t y = 0; y < var->yres; y++) {
		size_t offset = (size_t)(y + var->yoffset) * fix->line_length +
			(size_t)var->xoffset * bytes;
		size_t row_size = (size_t)var->xres * bytes;
		if (offset > fb_size || row_size > fb_size - offset)
			return;
		uint8_t *dst = fb + offset;
		for (uint32_t x = 0; x < var->xres; x++) {
			uint32_t p = pixels[(size_t)y * var->xres + x];
			uint32_t rgb = channel((p >> 16) & 255, var->red) |
				channel((p >> 8) & 255, var->green) |
				channel(p & 255, var->blue) |
				channel((p >> 24) & 255, var->transp);
			for (unsigned byte = 0; byte < bytes; byte++)
				dst[(size_t)x * bytes + byte] = (uint8_t)(rgb >> (byte * 8));
		}
	}
}

static enum boot_style selected_style(void)
{
	const char *env = getenv("ALGAE_BOOT_STYLE");
	char name[32] = "";
	if (env && *env)
		return boot_style_from_name(env);
	FILE *f = fopen("/etc/algae/boot-style", "r");
	if (f) {
		if (fscanf(f, "%31s", name) != 1)
			name[0] = '\0';
		fclose(f);
	}
	return boot_style_from_name(name);
}

int main(void)
{
	int fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("algae-boot-animation: open /dev/fb0");
		return 1;
	}
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
	    ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 ||
	    var.xres == 0 || var.yres == 0 ||
	    (var.bits_per_pixel != 16 && var.bits_per_pixel != 24 &&
	     var.bits_per_pixel != 32)) {
		fprintf(stderr, "algae-boot-animation: unsupported framebuffer mode\n");
		close(fd);
		return 1;
	}
	uint8_t *fb = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE,
			   MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED) {
		perror("algae-boot-animation: mmap framebuffer");
		close(fd);
		return 1;
	}
	uint32_t *pixels = calloc((size_t)var.xres * var.yres, sizeof(*pixels));
	if (!pixels) {
		perror("algae-boot-animation: allocate frame");
		munmap(fb, fix.smem_len);
		close(fd);
		return 1;
	}
	cairo_surface_t *surface = cairo_image_surface_create_for_data(
		(uint8_t *)pixels, CAIRO_FORMAT_ARGB32, var.xres, var.yres,
		(int)var.xres * 4);
	cairo_t *cr = cairo_create(surface);
	enum boot_style style = selected_style();
	if (style == BOOT_STYLE_SPRIG)
		style = BOOT_STYLE_LOGO;
	struct timespec start, frame_start;
	clock_gettime(CLOCK_MONOTONIC, &start);
	while (1) {
		clock_gettime(CLOCK_MONOTONIC, &frame_start);
		double elapsed = (double)(frame_start.tv_sec - start.tv_sec) +
			(double)(frame_start.tv_nsec - start.tv_nsec) / 1e9;
		cairo_pattern_t *bg = cairo_pattern_create_radial(
			var.xres * 0.5, var.yres * 0.44, 0,
			var.xres * 0.5, var.yres * 0.44,
			(var.xres > var.yres ? var.xres : var.yres) * 0.72);
		cairo_pattern_add_color_stop_rgb(bg, 0, 0.043, 0.231, 0.247);
		cairo_pattern_add_color_stop_rgb(bg, 1, 0.016, 0.086, 0.102);
		cairo_set_source(cr, bg);
		cairo_paint(cr);
		cairo_pattern_destroy(bg);
		boot_style_draw(cr, var.xres, var.yres, fmod(elapsed, 6.0), style);
		cairo_surface_flush(surface);
		present(fb, fix.smem_len, &fix, &var, pixels);
		struct timespec delay = { .tv_sec = 0, .tv_nsec = 33000000 };
		nanosleep(&delay, NULL);
	}
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	free(pixels);
	munmap(fb, fix.smem_len);
	close(fd);
	return 0;
}
