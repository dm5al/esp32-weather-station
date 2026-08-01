#include "ui/weather_icon.h"

#include "ui/fonts/ui_fonts.h"
#include "ui/ui.h"

/*
 * Icons are built from two primitives — a disc and a rounded bar — plus
 * LV_SYMBOL_CHARGE for the lightning bolt. No rotation is used anywhere,
 * because LVGL only transforms images reliably; the shapes are designed to read
 * correctly axis-aligned.
 *
 * All geometry is a fraction of the icon size, so the 128px hero icon and the
 * 48px forecast icons come out of the same code.
 *
 * Every icon sits on a rounded "sky" tile whose colour follows the conditions.
 * Without it, pale clouds float on the dark card with nothing to give them hue,
 * and overcast weather in particular reads as a grey smudge.
 */

/* ---- element colours ----------------------------------------------------- */

#define COL_SUN_TOP  lv_color_hex(0xFFF06B)
#define COL_SUN_BOT  lv_color_hex(0xFF8A00)
#define COL_RAY      lv_color_hex(0xFFE066)

#define COL_MOON_TOP lv_color_hex(0xFFFDF5)
#define COL_MOON_BOT lv_color_hex(0xD9E2F0)
#define COL_CRATER   lv_color_hex(0xC2CEE2)

/* Clouds are blue-tinted white, never neutral grey. */
#define COL_CLOUD_TOP lv_color_hex(0xFFFFFF)
#define COL_CLOUD_BOT lv_color_hex(0xBFD4EE)
#define COL_STORM_TOP lv_color_hex(0x93A9C9)
#define COL_STORM_BOT lv_color_hex(0x5C7397)

#define COL_RAIN_TOP lv_color_hex(0x9BE3FF)
#define COL_RAIN_BOT lv_color_hex(0x0284C7)
#define COL_SNOW     lv_color_hex(0xFFFFFF)
#define COL_BOLT     lv_color_hex(0xFFE83D)
#define COL_FOG      lv_color_hex(0xD6E2F0)

/* ---- sky tiles ----------------------------------------------------------- */

typedef struct {
    uint32_t top;
    uint32_t bottom;
} sky_t;

/** @brief Backdrop gradient for each condition: bright by day, deep at night. */
static sky_t sky_for(weather_icon_t kind)
{
    switch (kind) {
    case WICON_SUN:           return (sky_t){0x1E88E5, 0x7FD1F5};
    case WICON_PARTLY_DAY:    return (sky_t){0x2B84D8, 0x86C8EE};
    case WICON_MOON:          return (sky_t){0x111B3A, 0x2E4372};
    case WICON_PARTLY_NIGHT:  return (sky_t){0x16224A, 0x354C82};
    case WICON_CLOUD:         return (sky_t){0x46608A, 0x8AA4C4};
    case WICON_FOG:           return (sky_t){0x5A6E85, 0xA3B4C6};
    case WICON_DRIZZLE:       return (sky_t){0x2F5580, 0x6E9AC4};
    case WICON_RAIN:          return (sky_t){0x27486F, 0x5B85B4};
    case WICON_SNOW:          return (sky_t){0x54749C, 0xADC7E0};
    case WICON_THUNDER:       return (sky_t){0x1B2440, 0x44507A};
    default:                  return (sky_t){0x46608A, 0x8AA4C4};
    }
}

/* ---- animation helpers --------------------------------------------------- */

static void anim_x_cb(void *var, int32_t v)
{
    lv_obj_set_style_translate_x(var, v, 0);
}

static void anim_y_cb(void *var, int32_t v)
{
    lv_obj_set_style_translate_y(var, v, 0);
}

static void anim_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa(var, (lv_opa_t)v, 0);
}

/**
 * @brief Start a looping animation on @p obj.
 *
 * @param back Duration of the return leg; 0 means snap back and repeat, which
 *             is what falling precipitation wants. Non-zero gives a ping-pong,
 *             which suits drifting and pulsing.
 */
static void loop_anim(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t from, int32_t to,
                      uint32_t duration, uint32_t back, uint32_t delay)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_playback_duration(&a, back);
    lv_anim_set_delay(&a, delay);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* ---- primitives ---------------------------------------------------------- */

/** @brief Undecorated, non-scrollable, gradient-filled child object. */
static lv_obj_t *shape(lv_obj_t *parent, int w, int h, int x, int y, lv_color_t top,
                       lv_color_t bottom, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, top, 0);
    lv_obj_set_style_bg_grad_color(o, bottom, 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *flat(lv_obj_t *parent, int w, int h, int x, int y, lv_color_t c, int radius)
{
    return shape(parent, w, h, x, y, c, c, radius);
}

static lv_obj_t *disc(lv_obj_t *parent, int d, int x, int y, lv_color_t top, lv_color_t bottom)
{
    return shape(parent, d, d, x, y, top, bottom, LV_RADIUS_CIRCLE);
}

/**
 * @brief A cloud: three overlapping discs on a rounded slab, in one container
 *        so the whole thing drifts with a single animation.
 */
static lv_obj_t *draw_cloud(lv_obj_t *parent, int x, int y, int w, lv_color_t top,
                            lv_color_t bottom)
{
    int h = w * 62 / 100;
    int slab_h = h * 45 / 100;

    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    shape(c, w, slab_h, 0, h - slab_h, top, bottom, slab_h / 2);
    disc(c, w * 55 / 100, w * 18 / 100, 0, top, bottom);
    disc(c, w * 40 / 100, w * 58 / 100, h * 22 / 100, top, bottom);
    disc(c, w * 34 / 100, 0, h * 28 / 100, top, bottom);
    return c;
}

static void draw_sun(lv_obj_t *parent, int cx, int cy, int r, bool rays, bool animate)
{
    if (rays) {
        int len = r * 70 / 100;
        int thick = r * 32 / 100;
        if (thick < 2) {
            thick = 2;
        }
        int gap = r * 40 / 100;
        /* Four axis-aligned rays — diagonals would need rotation. */
        lv_obj_t *ray[4];
        ray[0] = flat(parent, thick, len, cx - thick / 2, cy - r - gap - len, COL_RAY, thick / 2);
        ray[1] = flat(parent, thick, len, cx - thick / 2, cy + r + gap, COL_RAY, thick / 2);
        ray[2] = flat(parent, len, thick, cx - r - gap - len, cy - thick / 2, COL_RAY, thick / 2);
        ray[3] = flat(parent, len, thick, cx + r + gap, cy - thick / 2, COL_RAY, thick / 2);

        if (animate) {
            /* Staggered opacity pulse reads as shimmer without moving anything,
             * which keeps the invalidated area tiny. */
            for (int i = 0; i < 4; i++) {
                loop_anim(ray[i], anim_opa_cb, 120, 255, 1400, 1400, i * 180);
            }
        }
    }
    /* Soft halo, then the disc itself. */
    lv_obj_t *halo = disc(parent, r * 5 / 2, cx - r * 5 / 4, cy - r * 5 / 4, COL_SUN_TOP,
                          COL_SUN_TOP);
    lv_obj_set_style_bg_opa(halo, LV_OPA_30, 0);
    disc(parent, r * 2, cx - r, cy - r, COL_SUN_TOP, COL_SUN_BOT);
}

/**
 * @brief Full moon with craters.
 *
 * A crescent would have to be carved by overpainting a disc in the backdrop
 * colour, which cannot match a gradient — it left a visible seam. Craters give
 * the same "this is the moon" read with no dependency on what is behind it.
 */
static void draw_moon(lv_obj_t *parent, int cx, int cy, int r)
{
    lv_obj_t *halo = disc(parent, r * 12 / 5, cx - r * 6 / 5, cy - r * 6 / 5, COL_MOON_TOP,
                          COL_MOON_TOP);
    lv_obj_set_style_bg_opa(halo, LV_OPA_20, 0);

    disc(parent, r * 2, cx - r, cy - r, COL_MOON_TOP, COL_MOON_BOT);

    int c1 = r * 34 / 100;
    int c2 = r * 22 / 100;
    int c3 = r * 16 / 100;
    lv_obj_t *cr[3];
    cr[0] = disc(parent, c1, cx - r * 55 / 100, cy - r * 30 / 100, COL_CRATER, COL_CRATER);
    cr[1] = disc(parent, c2, cx + r * 8 / 100, cy + r * 30 / 100, COL_CRATER, COL_CRATER);
    cr[2] = disc(parent, c3, cx + r * 25 / 100, cy - r * 55 / 100, COL_CRATER, COL_CRATER);
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_opa(cr[i], LV_OPA_60, 0);
    }
}

/** @brief Falling precipitation under a cloud. */
static void draw_drops(lv_obj_t *parent, int x, int y, int w, int count, bool snow, bool animate)
{
    int slot = w / count;
    int dw = slot * 26 / 100;
    if (dw < 3) {
        dw = 3;
    }
    int dh = snow ? dw : dw * 3;

    for (int i = 0; i < count; i++) {
        /* Stagger every other drop so it reads as falling, not as a comb. */
        int dy = y + ((i % 2) ? dh / 2 : 0);
        int dx = x + slot * i + slot / 2 - dw / 2;
        lv_obj_t *d = snow ? flat(parent, dw, dh, dx, dy, COL_SNOW, LV_RADIUS_CIRCLE)
                           : shape(parent, dw, dh, dx, dy, COL_RAIN_TOP, COL_RAIN_BOT, dw / 2);

        if (animate) {
            /* No return leg: the drop snaps back to the cloud and falls again. */
            uint32_t fall = snow ? 2600 : 1100;
            loop_anim(d, anim_y_cb, -dh, dh * 3, fall, 0, i * (fall / count));
            if (snow) {
                loop_anim(d, anim_x_cb, -2, 2, 1300, 1300, i * 200);
            }
        }
    }
}

static void draw_bolt(lv_obj_t *parent, int cx, int y, int size, bool animate)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(l, COL_BOLT, 0);
    lv_obj_set_style_text_font(l, size >= 90 ? &lv_font_ui_28 : &lv_font_ui_20, 0);
    lv_obj_update_layout(l);
    lv_obj_set_pos(l, cx - lv_obj_get_width(l) / 2, y);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);

    if (animate) {
        /* Quick strobe with a long pause between strikes. */
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, l);
        lv_anim_set_exec_cb(&a, anim_opa_cb);
        lv_anim_set_values(&a, 60, 255);
        lv_anim_set_duration(&a, 120);
        lv_anim_set_playback_duration(&a, 120);
        lv_anim_set_repeat_delay(&a, 1600);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}

lv_obj_t *weather_icon_create(lv_obj_t *parent, weather_icon_t kind, int size, bool animate)
{
    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, size, size);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    /* Keep drifting clouds and falling drops inside the tile. */
    lv_obj_set_style_clip_corner(root, true, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    const sky_t sky = sky_for(kind);
    shape(root, size, size, 0, 0, lv_color_hex(sky.top), lv_color_hex(sky.bottom), size / 5);

    const int s = size;
    const int mid = s / 2;
    lv_obj_t *cloud = NULL;

    switch (kind) {
    case WICON_SUN:
        draw_sun(root, mid, mid, s * 22 / 100, true, animate);
        break;

    case WICON_MOON:
        draw_moon(root, mid, mid, s * 28 / 100);
        break;

    case WICON_PARTLY_DAY:
        draw_sun(root, s * 34 / 100, s * 32 / 100, s * 15 / 100, true, animate);
        cloud = draw_cloud(root, s * 20 / 100, s * 42 / 100, s * 72 / 100, COL_CLOUD_TOP,
                           COL_CLOUD_BOT);
        break;

    case WICON_PARTLY_NIGHT:
        draw_moon(root, s * 34 / 100, s * 30 / 100, s * 17 / 100);
        cloud = draw_cloud(root, s * 20 / 100, s * 42 / 100, s * 72 / 100, COL_CLOUD_TOP,
                           COL_CLOUD_BOT);
        break;

    case WICON_CLOUD: {
        /* Two layers read as "overcast" rather than "one cloud". */
        lv_obj_t *back = draw_cloud(root, s * 26 / 100, s * 14 / 100, s * 60 / 100, COL_STORM_TOP,
                                    COL_STORM_BOT);
        cloud = draw_cloud(root, s * 4 / 100, s * 34 / 100, s * 74 / 100, COL_CLOUD_TOP,
                           COL_CLOUD_BOT);
        if (animate) {
            /* A different period stops the two layers moving as one slab. */
            loop_anim(back, anim_x_cb, -3, 3, 5200, 5200, 0);
        }
        break;
    }

    case WICON_FOG:
        cloud = draw_cloud(root, s * 12 / 100, s * 12 / 100, s * 76 / 100, COL_CLOUD_TOP,
                           COL_CLOUD_BOT);
        for (int i = 0; i < 3; i++) {
            int bar_w = (i == 1) ? s * 76 / 100 : s * 58 / 100;
            lv_obj_t *bar = flat(root, bar_w, s * 6 / 100, (s - bar_w) / 2, s * (66 + i * 12) / 100,
                                 COL_FOG, s * 3 / 100);
            if (animate) {
                loop_anim(bar, anim_x_cb, -4, 4, 3000, 3000, i * 400);
            }
        }
        break;

    case WICON_DRIZZLE:
        cloud = draw_cloud(root, s * 12 / 100, s * 10 / 100, s * 76 / 100, COL_CLOUD_TOP,
                           COL_CLOUD_BOT);
        draw_drops(root, s * 20 / 100, s * 62 / 100, s * 60 / 100, 2, false, animate);
        break;

    case WICON_RAIN:
        cloud = draw_cloud(root, s * 12 / 100, s * 8 / 100, s * 76 / 100, COL_CLOUD_TOP,
                           COL_CLOUD_BOT);
        draw_drops(root, s * 14 / 100, s * 60 / 100, s * 72 / 100, 3, false, animate);
        break;

    case WICON_SNOW:
        cloud = draw_cloud(root, s * 12 / 100, s * 8 / 100, s * 76 / 100, COL_CLOUD_TOP,
                           COL_CLOUD_BOT);
        draw_drops(root, s * 14 / 100, s * 62 / 100, s * 72 / 100, 3, true, animate);
        break;

    case WICON_THUNDER:
        cloud = draw_cloud(root, s * 12 / 100, s * 6 / 100, s * 76 / 100, COL_STORM_TOP,
                           COL_STORM_BOT);
        draw_bolt(root, mid, s * 52 / 100, s, animate);
        break;
    }

    /* Every cloud drifts, whatever it is sitting under. */
    if (animate && cloud) {
        loop_anim(cloud, anim_x_cb, -4, 4, 4000, 4000, 0);
    }

    return root;
}
