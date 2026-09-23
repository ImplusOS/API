#include "Material.h"

/*
 * Tonal-palette generation and the M3 token helpers. See Material.h for
 * what the tokens are; this file is the colour science and the role table.
 *
 * Freestanding on purpose: no libm. Square and cube roots are Newton
 * iterations, the sRGB transfer function is a 256-entry decode table, and
 * the encode direction is a binary search back through it -- which lands on
 * the nearest 8-bit code exactly, rather than approximately.
 */

/* ------------------------------------------------------------ sRGB EOTF */

static const float k_srgb_decode[256] = {
    0.0000000f, 0.0003035f, 0.0006071f, 0.0009106f, 0.0012141f, 0.0015176f,
    0.0018212f, 0.0021247f, 0.0024282f, 0.0027317f, 0.0030353f, 0.0033465f,
    0.0036765f, 0.0040247f, 0.0043914f, 0.0047770f, 0.0051815f, 0.0056054f,
    0.0060488f, 0.0065121f, 0.0069954f, 0.0074990f, 0.0080232f, 0.0085681f,
    0.0091341f, 0.0097212f, 0.0103298f, 0.0109601f, 0.0116122f, 0.0122865f,
    0.0129830f, 0.0137021f, 0.0144438f, 0.0152085f, 0.0159963f, 0.0168074f,
    0.0176420f, 0.0185002f, 0.0193824f, 0.0202886f, 0.0212190f, 0.0221739f,
    0.0231534f, 0.0241576f, 0.0251869f, 0.0262412f, 0.0273209f, 0.0284260f,
    0.0295568f, 0.0307134f, 0.0318960f, 0.0331048f, 0.0343398f, 0.0356013f,
    0.0368895f, 0.0382044f, 0.0395462f, 0.0409152f, 0.0423114f, 0.0437350f,
    0.0451862f, 0.0466651f, 0.0481718f, 0.0497066f, 0.0512695f, 0.0528606f,
    0.0544803f, 0.0561285f, 0.0578054f, 0.0595112f, 0.0612461f, 0.0630100f,
    0.0648033f, 0.0666259f, 0.0684782f, 0.0703601f, 0.0722719f, 0.0742136f,
    0.0761854f, 0.0781874f, 0.0802198f, 0.0822827f, 0.0843762f, 0.0865005f,
    0.0886556f, 0.0908417f, 0.0930590f, 0.0953075f, 0.0975873f, 0.0998987f,
    0.1022417f, 0.1046165f, 0.1070231f, 0.1094617f, 0.1119324f, 0.1144354f,
    0.1169707f, 0.1195384f, 0.1221388f, 0.1247718f, 0.1274377f, 0.1301365f,
    0.1328683f, 0.1356333f, 0.1384316f, 0.1412633f, 0.1441285f, 0.1470273f,
    0.1499598f, 0.1529262f, 0.1559265f, 0.1589608f, 0.1620294f, 0.1651322f,
    0.1682694f, 0.1714411f, 0.1746474f, 0.1778884f, 0.1811642f, 0.1844750f,
    0.1878208f, 0.1912017f, 0.1946178f, 0.1980693f, 0.2015563f, 0.2050787f,
    0.2086369f, 0.2122308f, 0.2158605f, 0.2195262f, 0.2232280f, 0.2269659f,
    0.2307400f, 0.2345506f, 0.2383976f, 0.2422811f, 0.2462013f, 0.2501583f,
    0.2541521f, 0.2581829f, 0.2622507f, 0.2663556f, 0.2704978f, 0.2746773f,
    0.2788943f, 0.2831487f, 0.2874408f, 0.2917706f, 0.2961383f, 0.3005438f,
    0.3049873f, 0.3094689f, 0.3139887f, 0.3185468f, 0.3231432f, 0.3277781f,
    0.3324515f, 0.3371636f, 0.3419144f, 0.3467041f, 0.3515326f, 0.3564001f,
    0.3613068f, 0.3662526f, 0.3712377f, 0.3762621f, 0.3813260f, 0.3864294f,
    0.3915725f, 0.3967552f, 0.4019778f, 0.4072402f, 0.4125426f, 0.4178851f,
    0.4232677f, 0.4286905f, 0.4341536f, 0.4396572f, 0.4452012f, 0.4507858f,
    0.4564110f, 0.4620770f, 0.4677838f, 0.4735315f, 0.4793202f, 0.4851499f,
    0.4910208f, 0.4969330f, 0.5028865f, 0.5088813f, 0.5149177f, 0.5209956f,
    0.5271151f, 0.5332764f, 0.5394795f, 0.5457245f, 0.5520114f, 0.5583404f,
    0.5647115f, 0.5711248f, 0.5775804f, 0.5840784f, 0.5906188f, 0.5972018f,
    0.6038273f, 0.6104956f, 0.6172066f, 0.6239604f, 0.6307571f, 0.6375969f,
    0.6444797f, 0.6514056f, 0.6583748f, 0.6653873f, 0.6724432f, 0.6795425f,
    0.6866853f, 0.6938718f, 0.7011019f, 0.7083758f, 0.7156935f, 0.7230551f,
    0.7304607f, 0.7379104f, 0.7454042f, 0.7529422f, 0.7605245f, 0.7681511f,
    0.7758222f, 0.7835378f, 0.7912979f, 0.7991027f, 0.8069523f, 0.8148466f,
    0.8227858f, 0.8307699f, 0.8387990f, 0.8468732f, 0.8549926f, 0.8631572f,
    0.8713671f, 0.8796224f, 0.8879231f, 0.8962694f, 0.9046612f, 0.9130987f,
    0.9215819f, 0.9301109f, 0.9386857f, 0.9473065f, 0.9559734f, 0.9646862f,
    0.9734453f, 0.9822506f, 0.9911021f, 1.0000000f,
};

static float srgb_decode(uint32_t channel)
{
    return k_srgb_decode[channel & 0xFFu];
}

/* Nearest 8-bit sRGB code for a linear value, by bisecting the decode
 * table. The table is monotonic, so eight steps settle it. */
static uint32_t srgb_encode(float linear)
{
    if (!(linear > 0.0f)) return 0u;      /* also catches NaN */
    if (linear >= 1.0f) return 255u;
    uint32_t low = 0u;
    uint32_t high = 255u;
    while (low + 1u < high) {
        uint32_t mid = (low + high) / 2u;
        if (k_srgb_decode[mid] <= linear) low = mid; else high = mid;
    }
    float below = linear - k_srgb_decode[low];
    float above = k_srgb_decode[high] - linear;
    return below <= above ? low : high;
}

/* --------------------------------------------------------------- roots */

static float m3_sqrt(float value)
{
    if (!(value > 0.0f)) return 0.0f;
    float guess = value > 1.0f ? value * 0.5f : 1.0f;
    for (uint32_t i = 0u; i < 24u; ++i) guess = 0.5f * (guess + value / guess);
    return guess;
}

static float m3_cbrt(float value)
{
    if (value == 0.0f) return 0.0f;
    bool negative = value < 0.0f;
    float magnitude = negative ? -value : value;
    float guess = magnitude > 1.0f ? magnitude : 1.0f;
    for (uint32_t i = 0u; i < 32u; ++i)
        guess = (2.0f * guess + magnitude / (guess * guess)) / 3.0f;
    return negative ? -guess : guess;
}

/* ----------------------------------------------------------- CIE L*a*b* */

/* D65, the white point sRGB is defined against. */
#define LAB_XN 0.95047f
#define LAB_YN 1.00000f
#define LAB_ZN 1.08883f
#define LAB_EPSILON 0.008856452f        /* 216/24389 */
#define LAB_KAPPA   903.2963f           /* 24389/27  */

static float lab_f(float t)
{
    return t > LAB_EPSILON ? m3_cbrt(t) : (LAB_KAPPA * t + 16.0f) / 116.0f;
}

static float lab_f_inverse(float t)
{
    float cubed = t * t * t;
    return cubed > LAB_EPSILON ? cubed : (116.0f * t - 16.0f) / LAB_KAPPA;
}

static void srgb_to_lab(uint32_t argb, float *out_l, float *out_a, float *out_b)
{
    float r = srgb_decode((argb >> 16u) & 0xFFu);
    float g = srgb_decode((argb >> 8u) & 0xFFu);
    float b = srgb_decode(argb & 0xFFu);

    float x = 0.4124564f * r + 0.3575761f * g + 0.1804375f * b;
    float y = 0.2126729f * r + 0.7151522f * g + 0.0721750f * b;
    float z = 0.0193339f * r + 0.1191920f * g + 0.9503041f * b;

    float fx = lab_f(x / LAB_XN);
    float fy = lab_f(y / LAB_YN);
    float fz = lab_f(z / LAB_ZN);

    *out_l = 116.0f * fy - 16.0f;
    *out_a = 500.0f * (fx - fy);
    *out_b = 200.0f * (fy - fz);
}

/* Lab -> linear sRGB, without clamping, so the caller can see whether the
 * colour fell outside the gamut. */
static void lab_to_linear(float l, float a, float b, float out[3])
{
    float fy = (l + 16.0f) / 116.0f;
    float fx = fy + a / 500.0f;
    float fz = fy - b / 200.0f;

    float x = LAB_XN * lab_f_inverse(fx);
    float y = LAB_YN * lab_f_inverse(fy);
    float z = LAB_ZN * lab_f_inverse(fz);

    out[0] =  3.2404542f * x - 1.5371385f * y - 0.4985314f * z;
    out[1] = -0.9692660f * x + 1.8760108f * y + 0.0415560f * z;
    out[2] =  0.0556434f * x - 0.2040259f * y + 1.0572252f * z;
}

static bool linear_in_gamut(const float rgb[3])
{
    for (uint32_t i = 0u; i < 3u; ++i)
        if (rgb[i] < -0.0001f || rgb[i] > 1.0001f) return false;
    return true;
}

/* The tone at lightness `l` along the hue direction (`a_hat`, `b_hat`),
 * with as much of `chroma` as sRGB can hold -- which is what keeps a
 * palette's light and dark ends from clipping into flat blocks. */
static uint32_t lab_tone_to_argb(float a_hat, float b_hat, float chroma, float l)
{
    float rgb[3];
    lab_to_linear(l, a_hat * chroma, b_hat * chroma, rgb);
    if (!linear_in_gamut(rgb)) {
        float low = 0.0f;
        float high = chroma;
        for (uint32_t i = 0u; i < 20u; ++i) {
            float mid = (low + high) * 0.5f;
            lab_to_linear(l, a_hat * mid, b_hat * mid, rgb);
            if (linear_in_gamut(rgb)) low = mid; else high = mid;
        }
        lab_to_linear(l, a_hat * low, b_hat * low, rgb);
    }
    return 0xFF000000u |
           (srgb_encode(rgb[0]) << 16u) |
           (srgb_encode(rgb[1]) << 8u) |
           srgb_encode(rgb[2]);
}

/* ---------------------------------------------------------- error tones */

/* M3's error palette is fixed rather than derived from the seed, and it
 * sits where LCh(ab) and HCT disagree most, so the published tones are
 * used verbatim and anything between them is interpolated. */
static const struct { uint8_t tone; uint32_t argb; } k_error_tones[] = {
    {  0u, 0xFF000000u }, { 10u, 0xFF410E0Bu }, { 20u, 0xFF601410u },
    { 30u, 0xFF8C1D18u }, { 40u, 0xFFB3261Eu }, { 50u, 0xFFDC362Eu },
    { 60u, 0xFFE46962u }, { 70u, 0xFFEC928Eu }, { 80u, 0xFFF2B8B5u },
    { 90u, 0xFFF9DEDCu }, { 95u, 0xFFFCEEEEu }, { 99u, 0xFFFFFBF9u },
    { 100u, 0xFFFFFFFFu },
};

static uint32_t channel_mix(uint32_t from, uint32_t to,
                            uint32_t shift, uint32_t numerator,
                            uint32_t denominator)
{
    uint32_t a = (from >> shift) & 0xFFu;
    uint32_t b = (to >> shift) & 0xFFu;
    uint32_t mixed = denominator == 0u ? a :
        (a * (denominator - numerator) + b * numerator + denominator / 2u) /
        denominator;
    return (mixed & 0xFFu) << shift;
}

static uint32_t error_tone(uint32_t tone)
{
    if (tone > 100u) tone = 100u;
    uint32_t count = (uint32_t)(sizeof(k_error_tones) / sizeof(k_error_tones[0]));
    for (uint32_t i = 0u; i + 1u < count; ++i) {
        uint32_t low = k_error_tones[i].tone;
        uint32_t high = k_error_tones[i + 1u].tone;
        if (tone < low || tone > high) continue;
        if (tone == low) return k_error_tones[i].argb;
        if (tone == high) return k_error_tones[i + 1u].argb;
        uint32_t from = k_error_tones[i].argb;
        uint32_t to = k_error_tones[i + 1u].argb;
        uint32_t numerator = tone - low;
        uint32_t denominator = high - low;
        return 0xFF000000u |
               channel_mix(from, to, 16u, numerator, denominator) |
               channel_mix(from, to, 8u, numerator, denominator) |
               channel_mix(from, to, 0u, numerator, denominator);
    }
    return k_error_tones[count - 1u].argb;
}

/* ---------------------------------------------------------- palette hue */

typedef struct {
    float a_hat;
    float b_hat;
    float chroma;
} m3_palette_t;

/* cos 60 deg / sin 60 deg -- the hue rotation M3 applies for tertiary. */
#define M3_TERTIARY_COS 0.5f
#define M3_TERTIARY_SIN 0.8660254f

static void palette_for(uint32_t seed_argb, m3_palette_kind_t kind,
                        m3_palette_t *out)
{
    float l, a, b;
    srgb_to_lab(seed_argb, &l, &a, &b);
    (void)l;
    float chroma = m3_sqrt(a * a + b * b);

    /* A greyscale seed has no hue to hold on to. Rather than inventing one,
     * every palette stays achromatic: M3's monochrome scheme, which is what
     * a user asking for a grey seed is after. */
    if (chroma < 1.0f) {
        out->a_hat = 1.0f;
        out->b_hat = 0.0f;
        out->chroma = 0.0f;
        return;
    }

    float a_hat = a / chroma;
    float b_hat = b / chroma;

    switch (kind) {
    case M3_PALETTE_PRIMARY:
        out->chroma = chroma > 48.0f ? chroma : 48.0f;
        break;
    case M3_PALETTE_SECONDARY:
        out->chroma = 16.0f;
        break;
    case M3_PALETTE_TERTIARY: {
        float rotated_a = a_hat * M3_TERTIARY_COS - b_hat * M3_TERTIARY_SIN;
        float rotated_b = a_hat * M3_TERTIARY_SIN + b_hat * M3_TERTIARY_COS;
        a_hat = rotated_a;
        b_hat = rotated_b;
        out->chroma = 24.0f;
        break;
    }
    case M3_PALETTE_NEUTRAL:
        out->chroma = 4.0f;
        break;
    case M3_PALETTE_NEUTRAL_VARIANT:
        out->chroma = 8.0f;
        break;
    case M3_PALETTE_ERROR:
    case M3_PALETTE_COUNT:
    default:
        out->chroma = 0.0f;
        break;
    }
    out->a_hat = a_hat;
    out->b_hat = b_hat;
}

uint32_t m3_tone(uint32_t seed_argb, m3_palette_kind_t palette,
                    uint32_t tone)
{
    if (tone > 100u) tone = 100u;
    /* The ends of every tonal palette are the achromatic ends of the
     * colour space, by definition. Deriving them instead would leave a
     * couple of code points of the seed's hue in them, because at L* 0 and
     * 100 the gamut check cannot tell a trace of chroma from none -- and
     * the scrim and shadow roles, which are tone 0, have to be black. */
    if (tone == 0u) return 0xFF000000u;
    if (tone == 100u) return 0xFFFFFFFFu;
    if (palette == M3_PALETTE_ERROR) return error_tone(tone);
    m3_palette_t p;
    palette_for(seed_argb, palette, &p);
    return lab_tone_to_argb(p.a_hat, p.b_hat, p.chroma, (float)tone);
}

/* ------------------------------------------------------------- schemes */

/* One row of the role table: which palette and which tone the role takes in
 * the light scheme and in the dark one. */
typedef struct {
    uint16_t offset;                 /* byte offset of the role in the scheme */
    uint8_t palette;
    uint8_t light_tone;
    uint8_t dark_tone;
} m3_role_t;

#define ROLE(field, palette_kind, light, dark) \
    { (uint16_t)__builtin_offsetof(m3_scheme_t, field), \
      (uint8_t)(palette_kind), (uint8_t)(light), (uint8_t)(dark) }

static const m3_role_t k_roles[] = {
    ROLE(primary,                  M3_PALETTE_PRIMARY,          40,  80),
    ROLE(on_primary,               M3_PALETTE_PRIMARY,         100,  20),
    ROLE(primary_container,        M3_PALETTE_PRIMARY,          90,  30),
    ROLE(on_primary_container,     M3_PALETTE_PRIMARY,          10,  90),

    ROLE(secondary,                M3_PALETTE_SECONDARY,        40,  80),
    ROLE(on_secondary,             M3_PALETTE_SECONDARY,       100,  20),
    ROLE(secondary_container,      M3_PALETTE_SECONDARY,        90,  30),
    ROLE(on_secondary_container,   M3_PALETTE_SECONDARY,        10,  90),

    ROLE(tertiary,                 M3_PALETTE_TERTIARY,         40,  80),
    ROLE(on_tertiary,              M3_PALETTE_TERTIARY,        100,  20),
    ROLE(tertiary_container,       M3_PALETTE_TERTIARY,         90,  30),
    ROLE(on_tertiary_container,    M3_PALETTE_TERTIARY,         10,  90),

    ROLE(error,                    M3_PALETTE_ERROR,            40,  80),
    ROLE(on_error,                 M3_PALETTE_ERROR,           100,  20),
    ROLE(error_container,          M3_PALETTE_ERROR,            90,  30),
    ROLE(on_error_container,       M3_PALETTE_ERROR,            10,  90),

    ROLE(surface,                  M3_PALETTE_NEUTRAL,          98,   6),
    ROLE(on_surface,               M3_PALETTE_NEUTRAL,          10,  90),
    ROLE(surface_variant,          M3_PALETTE_NEUTRAL_VARIANT,  90,  30),
    ROLE(on_surface_variant,       M3_PALETTE_NEUTRAL_VARIANT,  30,  80),
    ROLE(surface_dim,              M3_PALETTE_NEUTRAL,          87,   6),
    ROLE(surface_bright,           M3_PALETTE_NEUTRAL,          98,  24),

    ROLE(surface_container_lowest, M3_PALETTE_NEUTRAL,         100,   4),
    ROLE(surface_container_low,    M3_PALETTE_NEUTRAL,          96,  10),
    ROLE(surface_container,        M3_PALETTE_NEUTRAL,          94,  12),
    ROLE(surface_container_high,   M3_PALETTE_NEUTRAL,          92,  17),
    ROLE(surface_container_highest,M3_PALETTE_NEUTRAL,          90,  22),

    ROLE(outline,                  M3_PALETTE_NEUTRAL_VARIANT,  50,  60),
    ROLE(outline_variant,          M3_PALETTE_NEUTRAL_VARIANT,  80,  30),

    ROLE(inverse_surface,          M3_PALETTE_NEUTRAL,          20,  90),
    ROLE(inverse_on_surface,       M3_PALETTE_NEUTRAL,          95,  20),
    ROLE(inverse_primary,          M3_PALETTE_PRIMARY,          80,  40),

    ROLE(surface_tint,             M3_PALETTE_PRIMARY,          40,  80),
    ROLE(shadow,                   M3_PALETTE_NEUTRAL,           0,   0),
    ROLE(scrim,                    M3_PALETTE_NEUTRAL,           0,   0),
};

#undef ROLE

void m3_scheme_init(m3_scheme_t *out, uint32_t seed_argb, bool dark)
{
    if (!out) return;
    uint32_t count = (uint32_t)(sizeof(k_roles) / sizeof(k_roles[0]));
    for (uint32_t i = 0u; i < count; ++i) {
        const m3_role_t *role = &k_roles[i];
        uint32_t tone = dark ? role->dark_tone : role->light_tone;
        uint32_t value = m3_tone(seed_argb,
                                    (m3_palette_kind_t)role->palette, tone);
        *(uint32_t *)((unsigned char *)out + role->offset) = value;
    }
}

bool m3_scheme_is_dark(const m3_scheme_t *scheme)
{
    if (!scheme) return false;
    /* The surface is darker than what is written on it exactly when the
     * scheme is the dark one; no flag needed, and a hand-edited theme file
     * that overrides the roles is classified by what it actually looks
     * like rather than by what it declared. */
    uint32_t surface = scheme->surface;
    uint32_t on_surface = scheme->on_surface;
    uint32_t surface_sum = ((surface >> 16u) & 0xFFu) +
                           ((surface >> 8u) & 0xFFu) * 2u + (surface & 0xFFu);
    uint32_t on_sum = ((on_surface >> 16u) & 0xFFu) +
                      ((on_surface >> 8u) & 0xFFu) * 2u + (on_surface & 0xFFu);
    return surface_sum < on_sum;
}

/* ---------------------------------------------------------- type scale */

void m3_typescale_init(m3_typescale_t *out, float scale)
{
    if (!out) return;
    if (!(scale > 0.1f) || scale > 4.0f) scale = 1.0f;
    out->headline_medium = 28.0f * scale;
    out->headline_small  = 24.0f * scale;
    out->title_large     = 22.0f * scale;
    out->title_medium    = 16.0f * scale;
    out->title_small     = 14.0f * scale;
    out->body_large      = 16.0f * scale;
    out->body_medium     = 14.0f * scale;
    out->body_small      = 12.0f * scale;
    out->label_large     = 14.0f * scale;
    out->label_medium    = 12.0f * scale;
    out->label_small     = 11.0f * scale;
}

/* --------------------------------------------------- state / elevation */

uint32_t m3_alpha(uint32_t argb, uint32_t percent)
{
    if (percent > 100u) percent = 100u;
    uint32_t alpha = ((argb >> 24u) * percent + 50u) / 100u;
    return (argb & 0x00FFFFFFu) | ((alpha & 0xFFu) << 24u);
}

uint32_t m3_state_overlay(uint32_t on_color, uint32_t percent)
{
    if (percent > 100u) percent = 100u;
    uint32_t alpha = (255u * percent + 50u) / 100u;
    return (on_color & 0x00FFFFFFu) | ((alpha & 0xFFu) << 24u);
}

uint32_t m3_state_layer(uint32_t base, uint32_t on_color, uint32_t percent)
{
    if (percent == 0u) return base;
    if (percent > 100u) percent = 100u;
    uint32_t result = base & 0xFF000000u;
    for (uint32_t shift = 0u; shift <= 16u; shift += 8u)
        result |= channel_mix(base, on_color, shift, percent, 100u);
    return result;
}

/* M3's surface-tint opacities, one per elevation level. */
static const uint8_t k_tint_percent[M3_ELEVATION_COUNT] = {0u, 5u, 8u, 11u, 12u, 14u};
static const uint8_t k_shadow_spread[M3_ELEVATION_COUNT] = {0u, 3u, 6u, 10u, 14u, 20u};
static const uint8_t k_shadow_offset[M3_ELEVATION_COUNT] = {0u, 1u, 2u, 4u, 6u, 8u};

uint32_t m3_elevated(const m3_scheme_t *scheme, uint32_t container,
                        m3_elevation_t level)
{
    if (!scheme) return container;
    if (level >= M3_ELEVATION_COUNT) level = M3_ELEVATION_5;
    /* In the dark scheme the surface-container tones already carry the
     * elevation; adding tint on top of them is the double-counting M3
     * dropped when it replaced overlays with containers. */
    if (m3_scheme_is_dark(scheme)) return container;
    return m3_state_layer(container, scheme->surface_tint,
                             k_tint_percent[level]);
}

uint32_t m3_shadow_spread(m3_elevation_t level)
{
    if (level >= M3_ELEVATION_COUNT) level = M3_ELEVATION_5;
    return k_shadow_spread[level];
}

int32_t m3_shadow_offset_y(m3_elevation_t level)
{
    if (level >= M3_ELEVATION_COUNT) level = M3_ELEVATION_5;
    return (int32_t)k_shadow_offset[level];
}

/* --------------------------------------------------------------- shape */

uint32_t m3_shape_radius(uint32_t token, uint32_t width, uint32_t height)
{
    uint32_t shortest = width < height ? width : height;
    if (token == M3_SHAPE_FULL) return shortest / 2u;
    return token > shortest / 2u ? shortest / 2u : token;
}

/* --------------------------------------------------------------- motion */

/*
 * M3's easing curves are cubic Beziers with their end points at (0,0) and
 * (1,1), so only the two control points vary; progress arrives as the x of
 * that curve and what is wanted is its y, which takes a short Newton solve
 * for t.
 */
typedef struct { float x1, y1, x2, y2; } m3_curve_t;

static const m3_curve_t k_curves[M3_EASE_COUNT] = {
    [M3_EASE_STANDARD]              = {0.2f,  0.0f, 0.0f, 1.0f},
    [M3_EASE_EMPHASIZED_DECELERATE] = {0.05f, 0.7f, 0.1f, 1.0f},
    [M3_EASE_EMPHASIZED_ACCELERATE] = {0.3f,  0.0f, 0.8f, 0.15f},
};

static float bezier_axis(float t, float p1, float p2)
{
    float inverse = 1.0f - t;
    return 3.0f * inverse * inverse * t * p1 +
           3.0f * inverse * t * t * p2 +
           t * t * t;
}

static float bezier_axis_slope(float t, float p1, float p2)
{
    float inverse = 1.0f - t;
    return 3.0f * inverse * inverse * p1 +
           6.0f * inverse * t * (p2 - p1) +
           3.0f * t * t * (1.0f - p2);
}

float m3_ease(m3_easing_t curve, float progress)
{
    if (progress <= 0.0f) return 0.0f;
    if (progress >= 1.0f) return 1.0f;
    if (curve >= M3_EASE_COUNT) curve = M3_EASE_STANDARD;
    const m3_curve_t *c = &k_curves[curve];
    float t = progress;
    for (uint32_t i = 0u; i < 6u; ++i) {
        float error = bezier_axis(t, c->x1, c->x2) - progress;
        float slope = bezier_axis_slope(t, c->x1, c->x2);
        if (slope < 0.0001f && slope > -0.0001f) break;
        t -= error / slope;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    return bezier_axis(t, c->y1, c->y2);
}
