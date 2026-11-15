#define _GNU_SOURCE
#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "boot-styles.h"

struct app;

struct buffer_slot {
	struct app *app;
	struct wl_buffer *buffer;
	void *pixels;
	int busy;
};

struct app {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct wl_surface *cursor_surface;
	struct wl_buffer *cursor_buffer;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct buffer_slot buffers[2];
	int next_buffer, redraw_pending;
	int width, height, configured, selected, page, loading_done;
	double pointer_x, pointer_y;
	struct timespec loading_started;
	enum boot_style boot_style;
};

static void draw(struct app *app);

static void create_pointer_cursor(struct app *app)
{
	const int width = 24, height = 32, stride = width * 4;
	size_t size = (size_t)stride * height;
	int fd = (int)syscall(SYS_memfd_create, "algae-cursor", 1U);
	if (fd < 0 || ftruncate(fd, (off_t)size) < 0) {
		if (fd >= 0)
			close(fd);
		return;
	}
	uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) {
		close(fd);
		return;
	}
	cairo_surface_t *image = cairo_image_surface_create_for_data(
		(unsigned char *)pixels, CAIRO_FORMAT_ARGB32,
		width, height, stride);
	cairo_t *cr = cairo_create(image);
	cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_move_to(cr, 1, 1);
	cairo_line_to(cr, 1, 25);
	cairo_line_to(cr, 7, 19);
	cairo_line_to(cr, 12, 30);
	cairo_line_to(cr, 17, 28);
	cairo_line_to(cr, 12, 17);
	cairo_line_to(cr, 23, 17);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 0.02, 0.08, 0.08);
	cairo_fill(cr);
	cairo_move_to(cr, 3, 4);
	cairo_line_to(cr, 3, 21);
	cairo_line_to(cr, 8, 16);
	cairo_line_to(cr, 13, 27);
	cairo_line_to(cr, 15, 26);
	cairo_line_to(cr, 10, 15);
	cairo_line_to(cr, 20, 15);
	cairo_close_path(cr);
	cairo_set_source_rgb(cr, 0.96, 1.0, 0.98);
	cairo_fill(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(image);

	struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, fd, (int)size);
	app->cursor_buffer = wl_shm_pool_create_buffer(
		pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
	app->cursor_surface = wl_compositor_create_surface(app->compositor);
	wl_surface_attach(app->cursor_surface, app->cursor_buffer, 0, 0);
	wl_surface_damage(app->cursor_surface, 0, 0, width, height);
	wl_surface_commit(app->cursor_surface);
	wl_shm_pool_destroy(pool);
	munmap(pixels, size);
	close(fd);
}

static enum boot_style load_boot_style(void)
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

static const char *boot_style_name(enum boot_style style)
{
	static const char *names[] = { "logo", "kelp", "pixel", "sprig" };
	return names[style <= BOOT_STYLE_SPRIG ? style : BOOT_STYLE_LOGO];
}

static void save_boot_style(enum boot_style style)
{
	FILE *f = fopen("/etc/algae/boot-style", "w");
	if (f) {
		fprintf(f, "%s\n", boot_style_name(style));
		fclose(f);
	}
}

static void draw_text(cairo_t *cr, const char *text, double x, double y,
		     double size, double r, double g, double b)
{
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
			      CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, size);
	cairo_set_source_rgb(cr, r, g, b);
	cairo_move_to(cr, x, y);
	cairo_show_text(cr, text);
}

static double loading_seconds(struct app *app)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (double)(now.tv_sec - app->loading_started.tv_sec) +
		(double)(now.tv_nsec - app->loading_started.tv_nsec) / 1e9;
}

static void draw_leaf(cairo_t *cr, double angle, double length)
{
	cairo_save(cr);
	cairo_rotate(cr, angle);
	cairo_move_to(cr, 0, 0);
	cairo_curve_to(cr, length * 0.36, -length * 0.02,
		       length * 0.60, -length * 0.57,
		       length, -length * 0.72);
	cairo_curve_to(cr, length * 0.84, -length * 0.10,
		       length * 0.39, length * 0.12, 0, 0);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_restore(cr);
}

static void draw_loading(cairo_t *cr, double width, double height,
			 double elapsed)
{
	double scale = height / 800.0;
	double center_x = width / (2.0 * scale);
	double center_y = 320;
	double sway = sin(elapsed * 2.4) * 0.10;
	cairo_save(cr);
	cairo_scale(cr, scale, scale);
	cairo_set_source_rgb(cr, 0.22, 0.89, 0.64);
	cairo_set_line_width(cr, 7);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, center_x, center_y + 112);
	cairo_curve_to(cr, center_x - 20 * sway, center_y + 45,
		       center_x + 24 * sway, center_y - 28,
		       center_x, center_y - 124);
	cairo_stroke(cr);

	for (int i = 0; i < 5; i++) {
		double y = center_y + 68 - i * 43;
		double direction = (i % 2 == 0) ? -1.0 : 1.0;
		cairo_save(cr);
		cairo_translate(cr, center_x + (center_x ? sway * (y - center_y) : 0), y);
		cairo_set_source_rgb(cr, 0.27 + 0.035 * i, 0.83, 0.58 + 0.035 * i);
		draw_leaf(cr, direction * (0.48 + sway), 68 + 5 * sin(elapsed + i));
		cairo_restore(cr);
	}

	for (int i = 0; i < 4; i++) {
		double phase = elapsed * 1.7 + i * 1.57;
		double bx = center_x + sin(phase) * (82 + i * 9);
		double by = center_y - 120 - fmod(elapsed * 34 + i * 49, 250);
		double radius = 4 + (i % 2) * 2;
		cairo_set_source_rgba(cr, 0.54, 0.96, 0.81, 0.78);
		cairo_arc(cr, bx, by, radius, 0, 6.283185307);
		cairo_fill(cr);
	}

	draw_text(cr, "ALGAE OS", center_x - 69, 535, 24,
		  0.94, 0.99, 0.96);
	draw_text(cr, "GROWING YOUR SPACE", center_x - 96, 570, 13,
		  0.60, 0.83, 0.75);
	for (int i = 0; i < 3; i++) {
		double alpha = 0.30 + 0.70 * (0.5 + 0.5 * sin(elapsed * 5 - i * 1.2));
		cairo_set_source_rgba(cr, 0.40, 0.96, 0.72, alpha);
		cairo_arc(cr, center_x - 14 + i * 14, 612, 3.5, 0, 6.283185307);
		cairo_fill(cr);
	}
	cairo_restore(cr);
}

static void draw(struct app *app)
{
	if (!app->configured)
		return;
	struct buffer_slot *slot = NULL;
	for (int offset = 0; offset < 2; offset++) {
		int index = (app->next_buffer + offset) % 2;
		if (!app->buffers[index].busy) {
			slot = &app->buffers[index];
			app->next_buffer = (index + 1) % 2;
			break;
		}
	}
	if (!slot) {
		app->redraw_pending = 1;
		return;
	}
	cairo_surface_t *image = cairo_image_surface_create_for_data(
		slot->pixels, CAIRO_FORMAT_ARGB32, app->width, app->height,
		app->width * 4);
	cairo_t *cr = cairo_create(image);
	const double w = app->width, h = app->height;
	cairo_pattern_t *gradient = cairo_pattern_create_linear(0, 0, w, h);
	cairo_pattern_add_color_stop_rgb(gradient, 0, 0.025, 0.11, 0.15);
	cairo_pattern_add_color_stop_rgb(gradient, 1, 0.035, 0.24, 0.22);
	cairo_set_source(cr, gradient);
	cairo_paint(cr);
	cairo_pattern_destroy(gradient);
	double elapsed = loading_seconds(app);
	if (!app->loading_done && elapsed < 5.0) {
		if (app->boot_style == BOOT_STYLE_SPRIG)
			draw_loading(cr, w, h, elapsed);
		else
			boot_style_draw(cr, w, h, elapsed, app->boot_style);
		cairo_destroy(cr);
		cairo_surface_destroy(image);
		wl_surface_attach(app->surface, slot->buffer, 0, 0);
		wl_surface_damage(app->surface, 0, 0, app->width, app->height);
		wl_surface_commit(app->surface);
		slot->busy = 1;
		return;
	}
	app->loading_done = 1;

	/* Soft accent shapes keep the screen legible at different resolutions. */
	cairo_set_source_rgba(cr, 0.20, 0.82, 0.61, 0.10);
	cairo_arc(cr, w * 0.88, h * 0.16, h * 0.23, 0, 6.283185307);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 0.29, 0.68, 0.92, 0.08);
	cairo_arc(cr, w * 0.05, h * 0.91, h * 0.29, 0, 6.283185307);
	cairo_fill(cr);

	double scale = h / 800.0;
	cairo_save(cr);
	cairo_scale(cr, scale, scale);
	double logical_w = w / scale;
	draw_text(cr, "ALGAE OS", 72, 88, 22, 0.34, 0.91, 0.68);
	draw_text(cr, "A simple system, growing from the ground up", 72, 175,
		  39, 0.96, 0.98, 0.97);
	draw_text(cr, "Welcome. Your device is ready.", 74, 220, 19,
		  0.68, 0.81, 0.79);

	if (app->page == 0) {
		const char *titles[] = { "SERVICES", "APPLICATIONS", "SETTINGS" };
		const char *subtitles[] = {
			"Manage system services", "Your apps will live here", "Device preferences"
		};
		double margin = 72, gap = 22;
		double card_w = (logical_w - margin * 2 - gap * 2) / 3;
		for (int i = 0; i < 3; i++) {
			double x = margin + i * (card_w + gap), y = 305, card_h = 220;
			cairo_set_source_rgba(cr, 0.91, 0.97, 0.95, 0.10);
			cairo_rectangle(cr, x, y, card_w, card_h);
			cairo_fill(cr);
			cairo_set_line_width(cr, 2);
			cairo_set_source_rgba(cr, 0.43, 0.91, 0.72, 0.24);
			cairo_rectangle(cr, x, y, card_w, card_h);
			cairo_stroke(cr);
			cairo_set_source_rgb(cr, 0.25, 0.82, 0.63);
			cairo_arc(cr, x + 38, y + 45, 12, 0, 6.283185307);
			cairo_fill(cr);
			draw_text(cr, titles[i], x + 28, y + 104, 19, 0.94, 0.98, 0.96);
			draw_text(cr, subtitles[i], x + 28, y + 142, 13, 0.66, 0.79, 0.77);
		}
		draw_text(cr, "Select a tile to explore Algae OS", 74, 590, 17,
			  0.78, 0.91, 0.87);
	} else {
		cairo_set_source_rgba(cr, 0.91, 0.97, 0.95, 0.10);
		cairo_rectangle(cr, 72, 105, 124, 48);
		cairo_fill(cr);
		cairo_set_line_width(cr, 2);
		cairo_set_source_rgba(cr, 0.43, 0.91, 0.72, 0.45);
		cairo_rectangle(cr, 72, 105, 124, 48);
		cairo_stroke(cr);
		draw_text(cr, "<  HOME", 92, 135, 15, 0.94, 0.98, 0.96);
		if (app->page == 1) {
			draw_text(cr, "Services", 72, 220, 36, 0.96, 0.98, 0.97);
			draw_text(cr, "System services", 74, 275, 18, 0.43, 0.91, 0.72);
			draw_text(cr, "demo-service     clock-service     weston", 74, 325, 18,
				  0.88, 0.95, 0.93);
			draw_text(cr, "Check and control services from the console with:",
				  74, 395, 17, 0.68, 0.81, 0.79);
			draw_text(cr, "algae-service status", 74, 435, 17,
				  0.78, 0.91, 0.87);
		} else if (app->page == 2) {
			draw_text(cr, "Applications", 72, 220, 36, 0.96, 0.98, 0.97);
			draw_text(cr, "No applications are installed yet.", 74, 290, 20,
				  0.78, 0.91, 0.87);
			draw_text(cr, "This space is ready for Algae apps.", 74, 330, 17,
				  0.68, 0.81, 0.79);
		} else {
			draw_text(cr, "Settings", 72, 220, 36, 0.96, 0.98, 0.97);
			draw_text(cr, "Boot style", 74, 290, 18, 0.78, 0.91, 0.87);
			const char *styles[] = { "LOGO", "KELP", "PIXEL", "SPRIG" };
			for (int i = 0; i < 4; i++) {
				double x = 74 + i * 148;
				cairo_set_source_rgba(cr, 0.91, 0.97, 0.95,
					app->boot_style == (enum boot_style)i ? 0.26 : 0.10);
				cairo_rectangle(cr, x, 320, 132, 52);
				cairo_fill(cr);
				cairo_set_source_rgba(cr, 0.43, 0.91, 0.72,
					app->boot_style == (enum boot_style)i ? 0.95 : 0.35);
				cairo_set_line_width(cr, 2);
				cairo_rectangle(cr, x, 320, 132, 52);
				cairo_stroke(cr);
				draw_text(cr, styles[i], x + 20, 352, 14,
					0.94, 0.98, 0.96);
			}
			draw_text(cr, "The selected style is used on the next boot.",
				  74, 420, 16, 0.68, 0.81, 0.79);
		}
	}
	draw_text(cr, "QEMU ARM64  |  WAYLAND", 74, 748, 12,
		  0.48, 0.67, 0.64);
	cairo_restore(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(image);
	wl_surface_attach(app->surface, slot->buffer, 0, 0);
	wl_surface_damage(app->surface, 0, 0, app->width, app->height);
	wl_surface_commit(app->surface);
	slot->busy = 1;
}

static void buffer_release(void *data, struct wl_buffer *buffer)
{
	(void)buffer;
	struct buffer_slot *slot = data;
	struct app *app = slot->app;
	slot->busy = 0;
	if (app->redraw_pending) {
		app->redraw_pending = 0;
		draw(app);
	}
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release,
};

static void xdg_surface_configure(void *data, struct xdg_surface *surface,
				  uint32_t serial)
{
	struct app *app = data;
	xdg_surface_ack_configure(surface, serial);
	if (!app->buffers[0].buffer) {
		int stride = app->width * 4;
		size_t pixels_size = (size_t)stride * app->height;
		for (int i = 0; i < 2; i++) {
			struct buffer_slot *slot = &app->buffers[i];
			int fd = (int)syscall(SYS_memfd_create, "algae-home", 1U);
			if (fd < 0 || ftruncate(fd, (off_t)pixels_size) < 0) {
				perror("algae-home: create shared buffer");
				exit(1);
			}
			slot->pixels = mmap(NULL, pixels_size, PROT_READ | PROT_WRITE,
					    MAP_SHARED, fd, 0);
			if (slot->pixels == MAP_FAILED) {
				perror("algae-home: mmap shared buffer");
				exit(1);
			}
			struct wl_shm_pool *pool = wl_shm_create_pool(
				app->shm, fd, (int)pixels_size);
			slot->buffer = wl_shm_pool_create_buffer(
				pool, 0, app->width, app->height, stride,
				WL_SHM_FORMAT_ARGB8888);
			slot->app = app;
			wl_buffer_add_listener(slot->buffer, &buffer_listener, slot);
			wl_shm_pool_destroy(pool);
			close(fd);
		}
	}
	if (!app->configured)
		clock_gettime(CLOCK_MONOTONIC, &app->loading_started);
	app->configured = 1;
	draw(app);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base,
			 uint32_t serial)
{
	(void)data;
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = wm_base_ping,
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
			  uint32_t serial, struct wl_surface *surface,
			  wl_fixed_t x, wl_fixed_t y)
{
	(void)surface;
	struct app *app = data;
	app->pointer_x = wl_fixed_to_double(x);
	app->pointer_y = wl_fixed_to_double(y);
	if (app->cursor_surface)
		wl_pointer_set_cursor(pointer, serial, app->cursor_surface, 1, 1);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
			  uint32_t serial, struct wl_surface *surface)
{
	(void)data; (void)pointer; (void)serial; (void)surface;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
			   uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
	(void)pointer; (void)time;
	struct app *app = data;
	app->pointer_x = wl_fixed_to_double(x);
	app->pointer_y = wl_fixed_to_double(y);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
			   uint32_t serial, uint32_t time, uint32_t button,
			   uint32_t state)
{
	(void)pointer; (void)serial; (void)time;
	struct app *app = data;
	fprintf(stderr, "[algae-home] button=%u state=%u pointer=(%.1f,%.1f) page=%d\n",
		button, state, app->pointer_x, app->pointer_y, app->page);
	if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED)
		return;
	double scale = app->height / 800.0;
	double x = app->pointer_x / scale;
	double y = app->pointer_y / scale;
	if (app->page == 0 && y >= 305 && y <= 525) {
		double logical_w = app->width / scale;
		double gap = 22, margin = 72;
		double card_w = (logical_w - margin * 2 - gap * 2) / 3;
		for (int i = 0; i < 3; i++) {
			double left = margin + i * (card_w + gap);
			if (x >= left && x <= left + card_w) {
				app->selected = i + 1;
				app->page = i + 1;
				draw(app);
				break;
			}
		}
	}
	if (app->page != 0 && x >= 72 && x <= 196 && y >= 105 && y <= 153) {
		app->page = 0;
		app->selected = 0;
		draw(app);
	} else if (app->page == 3 && y >= 320 && y <= 372) {
		for (int i = 0; i < 4; i++) {
			double left = 74 + i * 148;
			if (x >= left && x <= left + 132) {
				app->boot_style = (enum boot_style)i;
				save_boot_style(app->boot_style);
				draw(app);
				break;
			}
		}
	}
	fprintf(stderr, "[algae-home] click handled at logical=(%.1f,%.1f), page=%d\n",
		x, y, app->page);
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
			 uint32_t time, uint32_t axis, wl_fixed_t value)
{
	(void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer)
{
	(void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
				uint32_t source)
{
	(void)data; (void)pointer; (void)source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
			      uint32_t time, uint32_t axis)
{
	(void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
				  uint32_t axis, int32_t discrete)
{
	(void)data; (void)pointer; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
			     uint32_t capabilities)
{
	struct app *app = data;
	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !app->pointer) {
		app->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(app->pointer, &pointer_listener, app);
	} else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && app->pointer) {
		wl_pointer_destroy(app->pointer);
		app->pointer = NULL;
	}
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
	(void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void registry_global(void *data, struct wl_registry *registry,
			    uint32_t name, const char *interface, uint32_t version)
{
	struct app *app = data;
	if (strcmp(interface, wl_compositor_interface.name) == 0)
		app->compositor = wl_registry_bind(registry, name,
						  &wl_compositor_interface,
						  version < 4 ? version : 4);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		app->wm_base = wl_registry_bind(registry, name,
						&xdg_wm_base_interface,
						version < 3 ? version : 3);
		xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		app->seat = wl_registry_bind(registry, name, &wl_seat_interface,
					     version < 5 ? version : 5);
		wl_seat_add_listener(app->seat, &seat_listener, app);
	}
}

static void registry_remove(void *data, struct wl_registry *registry,
			    uint32_t name)
{
	(void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_remove,
};

int main(void)
{
	struct app app = { .width = 1280, .height = 800 };
	app.boot_style = load_boot_style();
	app.display = wl_display_connect(NULL);
	if (!app.display) {
		fprintf(stderr, "algae-home: cannot connect to Wayland compositor\n");
		return 1;
	}
	struct wl_registry *registry = wl_display_get_registry(app.display);
	wl_registry_add_listener(registry, &registry_listener, &app);
	wl_display_roundtrip(app.display);
	if (!app.compositor || !app.shm || !app.wm_base) {
		fprintf(stderr, "algae-home: compositor lacks required Wayland globals\n");
		return 1;
	}
	create_pointer_cursor(&app);
	app.surface = wl_compositor_create_surface(app.compositor);
	app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
	xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
	app.toplevel = xdg_surface_get_toplevel(app.xdg_surface);
	xdg_toplevel_set_title(app.toplevel, "Algae OS");
	xdg_toplevel_set_app_id(app.toplevel, "org.algae.home");
	xdg_toplevel_set_fullscreen(app.toplevel, NULL);
	/* The first commit causes Weston to send the initial xdg configure. */
	wl_surface_commit(app.surface);
	struct pollfd display_fd = {
		.fd = wl_display_get_fd(app.display),
		.events = POLLIN,
	};
	for (;;) {
		if (wl_display_flush(app.display) < 0) {
			if (errno != EAGAIN) {
				perror("algae-home: flush Wayland requests");
				break;
			}
			display_fd.events = POLLIN | POLLOUT;
		} else {
			display_fd.events = POLLIN;
		}
		int ready = poll(&display_fd, 1, 50);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (ready > 0 && (display_fd.revents & POLLIN)) {
			if (wl_display_dispatch(app.display) == -1)
				break;
		} else if (ready > 0 &&
			   (display_fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
			break;
		}
		if (app.configured && !app.loading_done) {
			draw(&app);
		}
	}
	return 0;
}
