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

struct app {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_buffer *buffer;
	int buffer_busy, redraw_pending;
	void *pixels;
	size_t pixels_size;
	int width, height, configured, selected, loading_done;
	double pointer_x, pointer_y;
	struct timespec loading_started;
};

static void draw(struct app *app);

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
	if (!app->configured || !app->buffer)
		return;
	if (app->buffer_busy) {
		app->redraw_pending = 1;
		return;
	}
	cairo_surface_t *image = cairo_image_surface_create_for_data(
		app->pixels, CAIRO_FORMAT_ARGB32, app->width, app->height,
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
	if (!app->loading_done && elapsed < 3.5) {
		draw_loading(cr, w, h, elapsed);
		cairo_destroy(cr);
		cairo_surface_destroy(image);
		wl_surface_attach(app->surface, app->buffer, 0, 0);
		wl_surface_damage(app->surface, 0, 0, app->width, app->height);
		wl_surface_commit(app->surface);
		app->buffer_busy = 1;
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

	const char *titles[] = { "SERVICES", "APPLICATIONS", "SETTINGS" };
	const char *subtitles[] = {
		"Manage system services", "Your apps will live here", "Device preferences"
	};
	double margin = 72, gap = 22;
	double card_w = (logical_w - margin * 2 - gap * 2) / 3;
	for (int i = 0; i < 3; i++) {
		double x = margin + i * (card_w + gap), y = 305, card_h = 220;
		cairo_set_source_rgba(cr, 0.91, 0.97, 0.95,
				      app->selected == i + 1 ? 0.20 : 0.10);
		cairo_rectangle(cr, x, y, card_w, card_h);
		cairo_fill(cr);
		cairo_set_line_width(cr, 2);
		cairo_set_source_rgba(cr, 0.43, 0.91, 0.72,
				      app->selected == i + 1 ? 0.95 : 0.24);
		cairo_rectangle(cr, x, y, card_w, card_h);
		cairo_stroke(cr);
		cairo_set_source_rgb(cr, 0.25, 0.82, 0.63);
		cairo_arc(cr, x + 38, y + 45, 12, 0, 6.283185307);
		cairo_fill(cr);
		draw_text(cr, titles[i], x + 28, y + 104, 19, 0.94, 0.98, 0.96);
		draw_text(cr, subtitles[i], x + 28, y + 142, 13, 0.66, 0.79, 0.77);
	}

	if (app->selected) {
		const char *message = app->selected == 1 ?
			"Services are managed with algae-service on the console." :
			app->selected == 2 ? "The Algae application space is ready to grow." :
			"Device settings are coming in a future milestone.";
		draw_text(cr, message, 74, 590, 17, 0.78, 0.91, 0.87);
	} else {
		draw_text(cr, "Select a tile to explore Algae OS", 74, 590, 17,
			  0.78, 0.91, 0.87);
	}
	draw_text(cr, "QEMU ARM64  |  WAYLAND", 74, 748, 12,
		  0.48, 0.67, 0.64);
	cairo_restore(cr);
	cairo_destroy(cr);
	cairo_surface_destroy(image);
	wl_surface_attach(app->surface, app->buffer, 0, 0);
	wl_surface_damage(app->surface, 0, 0, app->width, app->height);
	wl_surface_commit(app->surface);
	app->buffer_busy = 1;
}

static void buffer_release(void *data, struct wl_buffer *buffer)
{
	(void)buffer;
	struct app *app = data;
	app->buffer_busy = 0;
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
	if (!app->buffer) {
		int stride = app->width * 4;
		app->pixels_size = (size_t)stride * app->height;
		int fd = (int)syscall(SYS_memfd_create, "algae-home", 1U);
		if (fd < 0 || ftruncate(fd, (off_t)app->pixels_size) < 0) {
			perror("algae-home: create shared buffer");
			exit(1);
		}
		app->pixels = mmap(NULL, app->pixels_size, PROT_READ | PROT_WRITE,
				   MAP_SHARED, fd, 0);
		if (app->pixels == MAP_FAILED) {
			perror("algae-home: mmap");
			exit(1);
		}
		struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, fd,
							      (int)app->pixels_size);
		app->buffer = wl_shm_pool_create_buffer(pool, 0, app->width,
							 app->height, stride,
							 WL_SHM_FORMAT_ARGB8888);
		wl_buffer_add_listener(app->buffer, &buffer_listener, app);
		wl_shm_pool_destroy(pool);
		close(fd);
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
	(void)pointer; (void)serial; (void)surface;
	struct app *app = data;
	app->pointer_x = wl_fixed_to_double(x);
	app->pointer_y = wl_fixed_to_double(y);
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
	if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED)
		return;
	double scale = app->height / 800.0;
	double x = app->pointer_x / scale;
	double y = app->pointer_y / scale;
	if (y >= 305 && y <= 525) {
		double logical_w = app->width / scale;
		double gap = 22, margin = 72;
		double card_w = (logical_w - margin * 2 - gap * 2) / 3;
		for (int i = 0; i < 3; i++) {
			double left = margin + i * (card_w + gap);
			if (x >= left && x <= left + card_w) {
				app->selected = i + 1;
				draw(app);
				break;
			}
		}
	}
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
			 uint32_t time, uint32_t axis, wl_fixed_t value)
{
	(void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
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
	app.surface = wl_compositor_create_surface(app.compositor);
	app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
	xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
	app.toplevel = xdg_surface_get_toplevel(app.xdg_surface);
	xdg_toplevel_set_title(app.toplevel, "Algae OS");
	xdg_toplevel_set_app_id(app.toplevel, "org.algae.home");
	if (app.seat)
		wl_display_roundtrip(app.display);
	wl_surface_commit(app.surface);
	struct pollfd display_fd = {
		.fd = wl_display_get_fd(app.display),
		.events = POLLIN,
	};
	for (;;) {
		int ready = poll(&display_fd, 1, 50);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (ready > 0) {
			if (wl_display_dispatch(app.display) == -1)
				break;
		} else if (app.configured && !app.loading_done) {
			draw(&app);
		}
	}
	return 0;
}
