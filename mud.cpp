/* mud - mini dither - written by wooosh - MIT licensed */

#include "spng.h"
#include "fpng.h"

#include <immintrin.h>
#include <sys/types.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

struct RGB {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a; /* ignored, only used for padding purposes */
};

static char *output_filename;
static RGB palette[255];
static uint32_t palette_radius[255];
static uint_fast8_t palette_len = 0;
static RGB *img = NULL;
static unsigned img_w, img_h;
static size_t img_len;

static RGB RGBFromUint32(uint32_t x) {
  RGB c;
  c.r = x >> 16;
  c.g = x >> 8;
  c.b = x >> 0;
  c.a = 0;
  return c;
}

static void Die(const char *msg) {
  fprintf(stderr, "fatal error: %s\n", msg);
  exit(EXIT_FAILURE);
}

static void HandleArguments(int argc, char **argv) {
  if (argc < 3)
    Die("syntax: mud <input filename> <output filename> <color1> [colorN]");
  argv++; /* skip argv[0] */

  char *filename = *argv;
  argv++;
  output_filename = *argv;
  argv++;

  FILE *png = fopen(filename, "rb");
  if (png == NULL)
    Die("error opening source image");

  spng_ctx *ctx = spng_ctx_new(0);
  if (ctx == NULL)
    Die("spng init failure");

  spng_set_png_file(ctx, png);
  
  struct spng_ihdr ihdr;
  if (spng_get_ihdr(ctx, &ihdr))
    Die("unable to read ihdr");

  img_w = ihdr.width;
  img_h = ihdr.height;

  if (spng_decoded_image_size(ctx, SPNG_FMT_RGBA8, &img_len))
    Die("unable to get image size");

  img = (RGB *) malloc(img_len);
  spng_decode_image(ctx, img, img_len, SPNG_FMT_RGBA8, 0); 

  /* load palette using remaining arguments */
  while (*argv) {
    char *arg = *argv;
    argv++;
    
    if (arg[0] == '#') arg++;
    /* TODO: error handling for strtoull, check that it has no alpha and 
     * formatted properly*/
    uint32_t color = strtoull(arg, NULL, 16);
    palette[palette_len] = RGBFromUint32(color);
    palette_len++;
  }
}

static  uint32_t RGBDistance(RGB x, RGB y) {
  int r_dist = x.r - y.r;
  int g_dist = x.g - y.g;
  int b_dist = x.b - y.b;

  return r_dist*r_dist + g_dist*g_dist + b_dist*b_dist;
}

static void ChooseColorRGB_Init(void) {
  /* calculate palette radius */
  for (uint_fast8_t i=0; i<palette_len; i++) {
    uint32_t min_dist = UINT32_MAX;
    for (uint_fast8_t j=0; j<palette_len; j++) {
      if (i == j) continue;
      uint32_t dist = RGBDistance(palette[i], palette[j]);
      if (dist < min_dist) {
        min_dist = dist;
      }
    }
    palette_radius[i] = min_dist;
  }
}

/* select the closest color from the palette using squared euclidean distance
 * in RGB colorspace */
static __attribute__((noinline)) RGB ChooseColorRGB(RGB c) {
  static RGB color;
  static uint32_t max_dist_reuse = 0;

  if (RGBDistance(c, color) < max_dist_reuse)
    return color;
 
  /* log2(255^2 + 225^2 + 255^2) < 32 */
  uint32_t min_dist = UINT32_MAX;
  uint_fast8_t i=0;
  uint_fast8_t best=0;
  color = palette[0];
  for (; i<palette_len; i++) {
    uint32_t dist = RGBDistance(c, palette[i]);
    if (dist < min_dist) {
      best = i;
      min_dist = dist;
    }
  }
  color = palette[best];
  max_dist_reuse = palette_radius[best];

  return color;
}

static void FloydSteinbergApply(uint_fast8_t n, int32_t *err, RGB *c) {
  const __m128i min_v = _mm_set1_epi32(0);
  const __m128i max_v = _mm_set1_epi32(255);
  const __m128i n_v = _mm_set1_epi32(n);
  
  const __m128i c_v = _mm_set_epi32(c->r, c->g, c->b, 0);
  const __m128i err_v = _mm_set_epi32(err[0], err[1], err[2], 0);

  __m128i result_v;
  /* ((err[i] * n)>>4) + c[i] */
  result_v = _mm_mullo_epi32(err_v, n_v);
  result_v = _mm_srai_epi32(result_v, 4);
  result_v = _mm_add_epi32(result_v, c_v);

  /* clamp to 0-255 */
  result_v = _mm_max_epi32(result_v, min_v);
  result_v = _mm_min_epi32(result_v, max_v);

  /* uint32_t[4] -> uint8_t[4] -> uint32_t -> RGB*/
  result_v = _mm_shuffle_epi8(result_v, _mm_set_epi8(
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 4, 8, 12
  ));
  uint32_t result_u32 = _mm_extract_epi32(result_v, 0) | 0xFF000000;
  memcpy(c, &result_u32, 4);
}

template<bool left, bool right, bool below>
static void inline FloydSteinbergIter(RGB *row, RGB *row_next, size_t x) {
  RGB orig = row[x];
  RGB c = ChooseColorRGB(orig);
  c.a = 255;
  int32_t err[4] = {
    (int) orig.r - c.r,
    (int) orig.g - c.g,
    (int) orig.b - c.b,
    0,
  };

  row[x] = c;
  if constexpr (right)
    FloydSteinbergApply(7, &err[0], row+x+1);

  if constexpr (below) {
    FloydSteinbergApply(5, &err[0], row_next+x);
    if constexpr (left) 
      FloydSteinbergApply(3, &err[0], row_next+x-1);
    if constexpr (right)
      FloydSteinbergApply(1, &err[0], row_next+x+1);
  }
}

static void __attribute__ ((noinline)) FloydSteinberg(void) {
  ChooseColorRGB_Init();
  for (size_t y=0; y<img_h-1; y++) {
    RGB *row = img + y*img_w;
    RGB *row_next = row + img_w;
    FloydSteinbergIter<false, true, true>(row, row_next, 0);
    for (size_t x=1; x<img_w-1; x++) {
      FloydSteinbergIter<true, true, true>(row, row_next, x);
    }
    FloydSteinbergIter<true, false, true>(row, row_next, 0);
  }
  
  RGB *row = img + (img_h-1)*img_w;
  FloydSteinbergIter<false, true, false>(row, NULL, 0);
  for (size_t x=1; x<img_w-1; x++) {
    FloydSteinbergIter<true, true, false>(row, NULL, x);
  }
  FloydSteinbergIter<true, false, false>(row, NULL, 0);
}

int main(int argc, char **argv) {
  HandleArguments(argc, argv); 
  FloydSteinberg();
  /* TODO: error checking */
  fpng::fpng_encode_image_to_file(output_filename, img, img_w, img_h, 4);
  free(img);
}
