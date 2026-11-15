/*
 * Algae OS boot styles: the animated loading mark shown by algae-home for
 * ALGAE_BOOT_SECONDS before the home screen appears.
 *
 * Styles: logo (three fronds), kelp (swaying strand), pixel (terminal boot).
 * Everything here is plain cairo so it can be previewed on a host machine
 * (see preview-boot-style.c) without Wayland.
 *
 * Drawing space is 800 units tall; callers pass the real surface size and
 * the functions scale to fit, like the original loading mark did.
 */
#ifndef ALGAE_BOOT_STYLES_H
#define ALGAE_BOOT_STYLES_H

#include <cairo/cairo.h>
#include <math.h>
#include <string.h>

#define ALGAE_BOOT_SECONDS 5.0

#define BS_TIDE  0.043, 0.431, 0.478
#define BS_KELP  0.055, 0.561, 0.561
#define BS_TEAL  0.122, 0.710, 0.627
#define BS_LIME  0.714, 0.953, 0.416
#define BS_MIST  0.910, 1.000, 0.965
#define BS_GOLD  0.961, 0.769, 0.318

enum boot_style {
	BOOT_STYLE_LOGO,
	BOOT_STYLE_KELP,
	BOOT_STYLE_PIXEL,
	BOOT_STYLE_SPRIG, /* the original sprig mark, kept as a legacy style */
};

static inline enum boot_style boot_style_from_name(const char *name)
{
	if (!name)
		return BOOT_STYLE_LOGO;
	if (!strcmp(name, "kelp"))
		return BOOT_STYLE_KELP;
	if (!strcmp(name, "pixel"))
		return BOOT_STYLE_PIXEL;
	if (!strcmp(name, "sprig"))
		return BOOT_STYLE_SPRIG;
	return BOOT_STYLE_LOGO;
}

/* ---- small helpers ---------------------------------------------------- */

static double bs_clamp(double v)
{
	return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

/* progress 0..1 of an event that starts at `start` and lasts `dur` seconds */
static double bs_seg(double t, double start, double dur)
{
	return bs_clamp((t - start) / dur);
}

static double bs_ease_out(double t)
{
	t = bs_clamp(t);
	return 1.0 - pow(1.0 - t, 3.0);
}

static double bs_ease_back(double t)
{
	const double c = 1.70158;
	t = bs_clamp(t);
	return 1.0 + (c + 1.0) * pow(t - 1.0, 3.0) + c * pow(t - 1.0, 2.0);
}

static double bs_text_width(cairo_t *cr, const char *text, const char *face,
			    int bold, double size)
{
	cairo_text_extents_t e;
	cairo_select_font_face(cr, face, CAIRO_FONT_SLANT_NORMAL,
			       bold ? CAIRO_FONT_WEIGHT_BOLD
				    : CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, size);
	cairo_text_extents(cr, text, &e);
	return e.x_advance;
}

static void bs_glow(cairo_t *cr, double cx, double cy, double radius, double t)
{
	double a = 0.38 * (0.75 + 0.25 * sin(t * 2.1));
	cairo_pattern_t *p = cairo_pattern_create_radial(cx, cy, 0, cx, cy, radius);
	cairo_pattern_add_color_stop_rgba(p, 0, BS_TEAL, a);
	cairo_pattern_add_color_stop_rgba(p, 1, BS_TEAL, 0);
	cairo_set_source(cr, p);
	cairo_arc(cr, cx, cy, radius, 0, 6.283185307);
	cairo_fill(cr);
	cairo_pattern_destroy(p);
}

static void bs_bubbles(cairo_t *cr, double cx, double t)
{
	static const double offset[6] = { -330, -250, -110, 60, 190, 300 };
	static const double radius[6] = { 7, 4, 9, 5, 8, 6 };
	for (int i = 0; i < 6; i++) {
		double y = 830 - fmod(t * 70.0 + i * 190.0, 900.0);
		double x = cx + offset[i] + sin(t * 1.3 + i) * 6.0;
		double fade = bs_clamp(1.0 - fabs(y - 400.0) / 520.0);
		cairo_set_source_rgba(cr, BS_TEAL, 0.55 * fade);
		cairo_set_line_width(cr, 1.5);
		cairo_arc(cr, x, y, radius[i], 0, 6.283185307);
		cairo_stroke(cr);
	}
}

/* "algae" in bold with a small spaced "OS", fading in at about 1.4 s */
static void bs_wordmark(cairo_t *cr, double cx, double y, double t)
{
	double a = bs_ease_out(bs_seg(t, 1.4, 0.9));
	if (t > 5.4)
		a *= bs_clamp((6.0 - t) / 0.6);
	if (a <= 0.0)
		return;
	const double gap = 14.0, spacing = 6.0;
	double w1 = bs_text_width(cr, "algae", "Sans", 1, 64);
	double wo = bs_text_width(cr, "O", "Sans", 0, 26);
	double ws = bs_text_width(cr, "S", "Sans", 0, 26);
	double x = cx - (w1 + gap + wo + spacing + ws) / 2.0;
	double yy = y + (1.0 - a) * 12.0;

	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
			       CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 64);
	cairo_set_source_rgba(cr, BS_MIST, a);
	cairo_move_to(cr, x, yy);
	cairo_show_text(cr, "algae");
	cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
			       CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 26);
	cairo_set_source_rgba(cr, BS_LIME, a);
	cairo_move_to(cr, x + w1 + gap, yy);
	cairo_show_text(cr, "O");
	cairo_move_to(cr, x + w1 + gap + wo + spacing, yy);
	cairo_show_text(cr, "S");
}

/* Set to 0 when rendering the psplash still: psplash draws its own bar. */
static int bs_show_progress = 1;

static void bs_progress(cairo_t *cr, double cx, double y, double t)
{
	const double w = 260.0;
	if (!bs_show_progress)
		return;
	double frac = bs_clamp(t / ALGAE_BOOT_SECONDS);
	double fade = t > 5.4 ? bs_clamp((6.0 - t) / 0.6) : 1.0;
	cairo_set_line_width(cr, 4);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_set_source_rgba(cr, 0.56, 0.84, 0.78, 0.18 * fade);
	cairo_move_to(cr, cx - w / 2, y);
	cairo_line_to(cr, cx + w / 2, y);
	cairo_stroke(cr);
	if (frac > 0.01) {
		cairo_pattern_t *p = cairo_pattern_create_linear(cx - w / 2, 0,
								 cx + w / 2, 0);
		cairo_pattern_add_color_stop_rgba(p, 0, BS_TEAL, fade);
		cairo_pattern_add_color_stop_rgba(p, 1, BS_LIME, fade);
		cairo_set_source(cr, p);
		cairo_move_to(cr, cx - w / 2, y);
		cairo_line_to(cr, cx - w / 2 + w * frac, y);
		cairo_stroke(cr);
		cairo_pattern_destroy(p);
	}
}

/* ---- style: logo ------------------------------------------------------ */

static void bs_logo_blade(cairo_t *cr, int which)
{
	if (which == 0) { /* left */
		cairo_move_to(cr, 56, 112);
		cairo_curve_to(cr, 28, 100, 16, 72, 22, 36);
		cairo_curve_to(cr, 46, 56, 54, 84, 56, 112);
	} else if (which == 1) { /* right */
		cairo_move_to(cr, 64, 112);
		cairo_curve_to(cr, 92, 100, 104, 72, 98, 36);
		cairo_curve_to(cr, 74, 56, 66, 84, 64, 112);
	} else { /* centre */
		cairo_move_to(cr, 60, 114);
		cairo_curve_to(cr, 42, 90, 42, 58, 60, 8);
		cairo_curve_to(cr, 78, 58, 78, 90, 60, 114);
	}
	cairo_close_path(cr);
}

static void boot_draw_logo(cairo_t *cr, double w, double h, double t)
{
	const double s = h / 800.0, cx = w / (2.0 * s), S = 2.6;
	static const double delay[3] = { 0.25, 0.5, 0.0 };

	cairo_save(cr);
	cairo_scale(cr, s, s);
	bs_glow(cr, cx, 330, 250, t);
	bs_bubbles(cr, cx, t);

	double sway = sin(t * 2.0) * 0.03 * bs_seg(t, 1.0, 1.0);
	double fade = t > 5.4 ? bs_clamp((6.0 - t) / 0.6) : 1.0;
	cairo_translate(cr, cx - 60 * S, 190 - 8 * S);
	cairo_scale(cr, S, S);
	cairo_translate(cr, 60, 114);
	cairo_rotate(cr, sway);
	cairo_translate(cr, -60, -114);

	for (int i = 0; i < 3; i++) {
		double g = bs_ease_out(bs_seg(t, delay[i], 1.5));
		if (g <= 0.0)
			continue;
		cairo_save(cr);
		cairo_translate(cr, 60, 112);
		cairo_scale(cr, 1.0, g);
		cairo_translate(cr, -60, -112);
		cairo_pattern_t *p;
		if (i == 2) {
			p = cairo_pattern_create_linear(0, 114, 0, 8);
			cairo_pattern_add_color_stop_rgba(p, 0, BS_TEAL, bs_clamp(g * 4) * fade);
			cairo_pattern_add_color_stop_rgba(p, 1, BS_LIME, bs_clamp(g * 4) * fade);
		} else {
			p = cairo_pattern_create_linear(0, 112, 0, 36);
			cairo_pattern_add_color_stop_rgba(p, 0, BS_TIDE, bs_clamp(g * 4) * fade);
			cairo_pattern_add_color_stop_rgba(p, 1, BS_TEAL, bs_clamp(g * 4) * fade);
		}
		bs_logo_blade(cr, i);
		cairo_set_source(cr, p);
		cairo_fill(cr);
		cairo_pattern_destroy(p);
		cairo_restore(cr);
	}
	cairo_restore(cr);

	cairo_save(cr);
	cairo_scale(cr, s, s);
	bs_wordmark(cr, cx, 570, t);
	bs_progress(cr, cx, 700, t);
	cairo_restore(cr);
}

/* ---- style: kelp ------------------------------------------------------ */

struct bs_kelp_blade {
	double ox, oy;                  /* attach point on the stem */
	double x1, y1, x2, y2, x3, y3;  /* first curve, x3/y3 is the tip */
	double x4, y4, x5, y5;          /* second curve back to the attach point */
};

static const struct bs_kelp_blade bs_kelp_blades[4] = {
	{ 54, 137, 34, 138, 18, 126, 14, 108, 34, 110, 50, 120 },
	{ 60, 120, 80, 122, 98, 112, 104, 94, 82, 94, 64, 104 },
	{ 54, 66, 36, 66, 22, 54, 20, 38, 38, 40, 52, 50 },
	{ 60, 48, 78, 48, 92, 38, 94, 22, 76, 24, 62, 34 },
};

/* first `t` (0..1) of a cubic, continuing from the current point */
static void bs_cubic_part(cairo_t *cr, double x0, double y0, double x1,
			  double y1, double x2, double y2, double x3,
			  double y3, double t)
{
	double ax = x0 + (x1 - x0) * t, ay = y0 + (y1 - y0) * t;
	double bx = x1 + (x2 - x1) * t, by = y1 + (y2 - y1) * t;
	double cx = x2 + (x3 - x2) * t, cy = y2 + (y3 - y2) * t;
	double dx = ax + (bx - ax) * t, dy = ay + (by - ay) * t;
	double ex = bx + (cx - bx) * t, ey = by + (cy - by) * t;
	double fx = dx + (ex - dx) * t, fy = dy + (ey - dy) * t;
	cairo_curve_to(cr, ax, ay, dx, dy, fx, fy);
}

static void boot_draw_kelp(cairo_t *cr, double w, double h, double t)
{
	const double s = h / 800.0, cx = w / (2.0 * s), S = 2.2;

	cairo_save(cr);
	cairo_scale(cr, s, s);
	bs_glow(cr, cx, 300, 250, t);
	bs_bubbles(cr, cx, t);

	cairo_translate(cr, cx - 60 * S, 120);
	cairo_scale(cr, S, S);
	double sway = sin(t * 2.1) * 0.04 * bs_seg(t, 1.4, 1.0);
	cairo_translate(cr, 60, 156);
	cairo_rotate(cr, sway);
	cairo_translate(cr, -60, -156);

	for (int i = 0; i < 4; i++) {
		const struct bs_kelp_blade *b = &bs_kelp_blades[i];
		double p = bs_seg(t, 0.5 + 0.3 * i, 0.7);
		double k = bs_ease_back(p);
		if (p <= 0.0)
			continue;
		cairo_save(cr);
		cairo_translate(cr, b->ox, b->oy);
		cairo_scale(cr, k, k);
		cairo_translate(cr, -b->ox, -b->oy);
		cairo_move_to(cr, b->ox, b->oy);
		cairo_curve_to(cr, b->x1, b->y1, b->x2, b->y2, b->x3, b->y3);
		cairo_curve_to(cr, b->x4, b->y4, b->x5, b->y5, b->ox, b->oy);
		cairo_close_path(cr);
		cairo_pattern_t *g = cairo_pattern_create_linear(0, b->oy, 0, b->y3);
		cairo_pattern_add_color_stop_rgba(g, 0, BS_KELP, bs_clamp(p * 3));
		cairo_pattern_add_color_stop_rgba(g, 1, BS_LIME, bs_clamp(p * 3));
		cairo_set_source(cr, g);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
		cairo_restore(cr);
	}

	double u = bs_ease_out(bs_seg(t, 0.1, 1.7));
	if (u > 0.0) {
		cairo_pattern_t *g = cairo_pattern_create_linear(0, 156, 0, 8);
		cairo_pattern_add_color_stop_rgb(g, 0, BS_TIDE);
		cairo_pattern_add_color_stop_rgb(g, 1, BS_TEAL);
		cairo_set_source(cr, g);
		cairo_set_line_width(cr, 5);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_move_to(cr, 60, 156);
		if (u <= 0.5) {
			bs_cubic_part(cr, 60, 156, 40, 130, 80, 110, 60, 84, u * 2);
		} else {
			cairo_curve_to(cr, 40, 130, 80, 110, 60, 84);
			bs_cubic_part(cr, 60, 84, 40, 58, 78, 40, 62, 8,
				      (u - 0.5) * 2);
		}
		cairo_stroke(cr);
		cairo_pattern_destroy(g);
	}

	static const double bl[2][4] = { { 65, 106, 5.5, 1.2 },
					 { 55.5, 59, 4.5, 1.5 } };
	for (int i = 0; i < 2; i++) {
		double p = bs_seg(t, bl[i][3], 0.6);
		if (p <= 0.0)
			continue;
		cairo_set_source_rgb(cr, BS_GOLD);
		cairo_arc(cr, bl[i][0], bl[i][1], bl[i][2] * bs_ease_back(p),
			  0, 6.283185307);
		cairo_fill(cr);
	}

	static const double air[2][3] = { { 82, 16, 3 }, { 92, 22, 2 } };
	for (int i = 0; i < 2; i++) {
		double f = fmod(t * 0.45 + i * 0.5, 1.0);
		cairo_set_source_rgba(cr, BS_MIST, 0.8 * sin(f * 3.14159265));
		cairo_arc(cr, air[i][0], air[i][1] - 26.0 * f, air[i][2], 0,
			  6.283185307);
		cairo_fill(cr);
	}
	cairo_restore(cr);

	cairo_save(cr);
	cairo_scale(cr, s, s);
	bs_wordmark(cr, cx, 590, t);
	bs_progress(cr, cx, 700, t);
	cairo_restore(cr);
}

/* ---- style: pixel ----------------------------------------------------- */

struct bs_cell {
	unsigned char x, y, w, green;
};

/* 12x12 pixel frond; every rectangle is one row tall */
static const struct bs_cell bs_pixel_frond[] = {
	{ 5, 0, 2, 0 }, { 4, 1, 4, 0 }, { 4, 2, 4, 0 }, { 3, 3, 6, 0 },
	{ 3, 4, 6, 0 }, { 4, 5, 4, 0 }, { 4, 6, 4, 0 }, { 5, 7, 2, 0 },
	{ 5, 8, 2, 0 }, { 5, 9, 2, 0 }, { 5, 10, 2, 0 }, { 5, 11, 2, 0 },
	{ 1, 3, 1, 1 }, { 1, 4, 2, 1 }, { 1, 5, 2, 1 }, { 2, 6, 2, 1 },
	{ 3, 7, 2, 1 }, { 3, 8, 2, 1 },
	{ 10, 3, 1, 1 }, { 9, 4, 2, 1 }, { 9, 5, 2, 1 }, { 8, 6, 2, 1 },
	{ 7, 7, 2, 1 }, { 7, 8, 2, 1 },
};

static void boot_draw_pixel(cairo_t *cr, double w, double h, double t)
{
	const double s = h / 800.0, cx = w / (2.0 * s), C = 20.0;
	cairo_save(cr);
	cairo_set_source_rgb(cr, 0.031, 0.031, 0.047);
	cairo_paint(cr);
	cairo_scale(cr, s, s);

	/* the frond grows one pixel row at a time, bottom first */
	int rows = (int)(bs_seg(t, 0.3, 2.0) * 12.0 + 0.0001);
	double x0 = cx - 6 * C, y0 = 140;
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
	for (unsigned i = 0; i < sizeof(bs_pixel_frond) / sizeof(bs_pixel_frond[0]); i++) {
		const struct bs_cell *c = &bs_pixel_frond[i];
		if (c->y < 12 - rows)
			continue;
		if (c->green)
			cairo_set_source_rgb(cr, 0.204, 0.827, 0.600);
		else
			cairo_set_source_rgb(cr, 0.639, 0.902, 0.208);
		cairo_rectangle(cr, x0 + c->x * C, y0 + c->y * C, c->w * C, C);
		cairo_fill(cr);
	}
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);

	static const char *line[3] = { "kernel", "display", "network" };
	const double lx = cx - 130.0, size = 20.0;
	double okw = bs_text_width(cr, "[ ok ]", "Monospace", 0, size);
	for (int i = 0; i < 3; i++) {
		if (t < 1.0 + 0.8 * i)
			continue;
		double y = 460.0 + 36.0 * i;
		cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
				       CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, size);
		cairo_set_source_rgb(cr, 0.639, 0.902, 0.208);
		cairo_move_to(cr, lx, y);
		cairo_show_text(cr, "[ ok ]");
		cairo_set_source_rgb(cr, 0.612, 0.639, 0.686);
		cairo_move_to(cr, lx + okw + 10, y);
		cairo_show_text(cr, line[i]);
	}
	if (t >= 3.4) {
		double y = 460.0 + 36.0 * 3;
		const char *msg = "starting algae_os";
		cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
				       CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, size);
		cairo_set_source_rgb(cr, 0.898, 0.906, 0.922);
		cairo_move_to(cr, lx, y);
		cairo_show_text(cr, msg);
		if (fmod(t, 1.0) < 0.5) {
			double mw = bs_text_width(cr, msg, "Monospace", 0, size);
			cairo_set_source_rgb(cr, 0.639, 0.902, 0.208);
			cairo_rectangle(cr, lx + mw + 4, y - 18, 11, 22);
			cairo_fill(cr);
		}
	}
	cairo_restore(cr);
}

/* ---- dispatcher (logo, kelp and pixel; sprig stays in main.c) ---------- */

static void boot_style_draw(cairo_t *cr, double w, double h, double t,
			    enum boot_style style)
{
	switch (style) {
	case BOOT_STYLE_KELP:
		boot_draw_kelp(cr, w, h, t);
		break;
	case BOOT_STYLE_PIXEL:
		boot_draw_pixel(cr, w, h, t);
		break;
	default:
		boot_draw_logo(cr, w, h, t);
		break;
	}
}

#endif /* ALGAE_BOOT_STYLES_H */
