#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Material Design 3 design tokens for ImplusOS.
 *
 * Every colour, corner, text size, state layer, elevation and easing curve
 * the system draws comes from here -- the window manager's taskbar, Start
 * menu, decorations, dialogs and sheets, the login screen, and every
 * application built on ImUI -- so the desktop is one design system rather
 * than a set of palettes that happen to resemble each other.
 *
 * The colour system is M3's: five tonal palettes (primary, secondary,
 * tertiary, neutral, neutral-variant) generated from one seed colour, plus
 * the fixed error palette, mapped onto the standard role names by
 * m3_scheme_init(). Tones are produced in CIE LCh(ab) with the hue and
 * chroma of the seed held constant and the chroma reduced until the tone is
 * inside sRGB -- an approximation of M3's HCT that lands within a couple of
 * code points of the published baseline palettes.
 *
 * Colour format throughout: 0xAARRGGBB.
 */

/* ------------------------------------------------------------------ seed */

/* M3's baseline seed (#6750A4). m3_scheme_init() with this reproduces
 * the baseline scheme from the specification. */
#define M3_SEED_BASELINE 0xFF6750A4u

/* ----------------------------------------------------------------- shape */

/* M3 shape scale, in pixels. M3_SHAPE_FULL is a stadium: resolved
 * against the component's own box by m3_shape_radius(). */
#define M3_SHAPE_NONE           0u
#define M3_SHAPE_EXTRA_SMALL    4u
#define M3_SHAPE_SMALL          8u
#define M3_SHAPE_MEDIUM        12u
#define M3_SHAPE_LARGE         16u
#define M3_SHAPE_EXTRA_LARGE   28u
#define M3_SHAPE_FULL       10000u

/* ----------------------------------------------------------- state layer */

/* M3 state-layer opacities, in percent of the "on" colour laid over the
 * component's own container. */
#define M3_STATE_HOVER    8u
#define M3_STATE_FOCUS   10u
#define M3_STATE_PRESSED 10u
#define M3_STATE_DRAGGED 16u

/* M3 content opacities for a disabled component. */
#define M3_DISABLED_CONTENT   38u
#define M3_DISABLED_CONTAINER 12u

/* ---------------------------------------------------------------- motion */

/* M3 duration tokens, in milliseconds. M3 pairs a longer duration with the
 * emphasized-decelerate curve on the way in and a shorter one with
 * emphasized-accelerate on the way out, which is why something appearing
 * takes noticeably longer than the same thing leaving. */
#define M3_DURATION_SHORT_2   100u
#define M3_DURATION_SHORT_4   200u
#define M3_DURATION_MEDIUM_1  250u
#define M3_DURATION_MEDIUM_2  300u
#define M3_DURATION_MEDIUM_4  400u
#define M3_DURATION_LONG_2    500u

typedef enum {
    M3_EASE_STANDARD = 0,           /* most in-place changes            */
    M3_EASE_EMPHASIZED_DECELERATE,  /* things arriving on screen        */
    M3_EASE_EMPHASIZED_ACCELERATE,  /* things leaving it                */
    M3_EASE_COUNT
} m3_easing_t;

/* ------------------------------------------------------------- elevation */

typedef enum {
    M3_ELEVATION_0 = 0,   /* flat on the surface                        */
    M3_ELEVATION_1,       /* cards at rest                              */
    M3_ELEVATION_2,       /* window decorations, top app bars on scroll */
    M3_ELEVATION_3,       /* menus, dialogs, sheets                     */
    M3_ELEVATION_4,
    M3_ELEVATION_5,
    M3_ELEVATION_COUNT
} m3_elevation_t;

/* ------------------------------------------------------------- palettes */

typedef enum {
    M3_PALETTE_PRIMARY = 0,
    M3_PALETTE_SECONDARY,
    M3_PALETTE_TERTIARY,
    M3_PALETTE_NEUTRAL,
    M3_PALETTE_NEUTRAL_VARIANT,
    M3_PALETTE_ERROR,
    M3_PALETTE_COUNT
} m3_palette_kind_t;

/* --------------------------------------------------------- colour roles */

typedef struct {
    uint32_t primary;
    uint32_t on_primary;
    uint32_t primary_container;
    uint32_t on_primary_container;

    uint32_t secondary;
    uint32_t on_secondary;
    uint32_t secondary_container;
    uint32_t on_secondary_container;

    uint32_t tertiary;
    uint32_t on_tertiary;
    uint32_t tertiary_container;
    uint32_t on_tertiary_container;

    uint32_t error;
    uint32_t on_error;
    uint32_t error_container;
    uint32_t on_error_container;

    uint32_t surface;
    uint32_t on_surface;
    uint32_t surface_variant;
    uint32_t on_surface_variant;
    uint32_t surface_dim;
    uint32_t surface_bright;

    uint32_t surface_container_lowest;
    uint32_t surface_container_low;
    uint32_t surface_container;
    uint32_t surface_container_high;
    uint32_t surface_container_highest;

    uint32_t outline;
    uint32_t outline_variant;

    uint32_t inverse_surface;
    uint32_t inverse_on_surface;
    uint32_t inverse_primary;

    uint32_t surface_tint;
    uint32_t shadow;
    uint32_t scrim;
} m3_scheme_t;

/* ---------------------------------------------------------- type scale */

/* The M3 type scale, in pixels. The shell needs the title/body/label
 * groups; the display and headline ends are there for dialogs and empty
 * states. */
typedef struct {
    float headline_medium;
    float headline_small;
    float title_large;
    float title_medium;
    float title_small;
    float body_large;
    float body_medium;
    float body_small;
    float label_large;
    float label_medium;
    float label_small;
} m3_typescale_t;

/* ------------------------------------------------------------------ API */

/* One tone (0-100) of one of the seed's tonal palettes, opaque. */
uint32_t m3_tone(uint32_t seed_argb, m3_palette_kind_t palette,
                    uint32_t tone);

/* Fill `out` with the light or dark scheme for `seed_argb`. */
void m3_scheme_init(m3_scheme_t *out, uint32_t seed_argb, bool dark);

/* The M3 type scale multiplied by `scale` (1.0 = the specified sizes). */
void m3_typescale_init(m3_typescale_t *out, float scale);

/* `on_color` laid over `base` at one of the M3_STATE_* opacities. Both
 * are opaque role colours; the result is opaque. */
uint32_t m3_state_layer(uint32_t base, uint32_t on_color, uint32_t percent);

/* A state layer kept translucent, for drawing over content the shell does
 * not own (a window's own surface, the wallpaper). */
uint32_t m3_state_overlay(uint32_t on_color, uint32_t percent);

/* M3 surface-tint elevation: `surface_tint` blended into a container at the
 * level's tint opacity. In the dark scheme the higher container tones carry
 * the elevation instead, so this is a no-op there. */
uint32_t m3_elevated(const m3_scheme_t *scheme, uint32_t container,
                        m3_elevation_t level);

/* Key-light spread and y-offset, in pixels, for a level's drop shadow. */
uint32_t m3_shadow_spread(m3_elevation_t level);
int32_t  m3_shadow_offset_y(m3_elevation_t level);

/* Resolve a shape token (including M3_SHAPE_FULL) against a box. */
uint32_t m3_shape_radius(uint32_t token, uint32_t width, uint32_t height);

/* `argb` with its alpha scaled to `percent` of opaque. */
uint32_t m3_alpha(uint32_t argb, uint32_t percent);

/* True when `scheme` was built dark; used where a component has to pick a
 * tone by contrast rather than by role. */
bool m3_scheme_is_dark(const m3_scheme_t *scheme);

/* `progress` (0..1) shaped by one of M3's easing curves. */
float m3_ease(m3_easing_t curve, float progress);

/* ------------------------------------------------------- desktop theme */

/*
 * Read the seed and mode the desktop is currently themed with and build the
 * matching scheme, so an application looks like the shell it is running in
 * rather than like the shell's defaults. Falls back to the M3 baseline
 * light scheme when no theme file can be read, and always leaves `scheme`
 * and `type` complete.
 *
 * Declared here but implemented in MaterialTheme.c, which is the only part
 * of this module that touches the filesystem -- everything above is pure
 * arithmetic and is unit-tested on the host as such.
 */
void m3_load_desktop_theme(m3_scheme_t *scheme, m3_typescale_t *type);

/* The same resolution, as the two values themselves rather than as a
 * built scheme: `seed` gets the colour the desktop is themed with (M3's
 * baseline when no theme file could be read) and `dark` the variant.
 * Settings needs to say *which* of its swatches is the current one, and a
 * scheme alone does not carry that. Either out pointer may be NULL. */
void m3_get_desktop_theme(uint32_t *seed, bool *dark);
