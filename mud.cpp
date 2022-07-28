#include "spng.h"
#include "fpng.h"

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

/* select the closest color from the palette using squared euclidean distance
 * in RGB colorspace */
static RGB ChooseColorRGB(RGB c) {
  /* log2(255^2 + 225^2 + 255^2) < 32 */
  uint32_t min_dist = UINT32_MAX;
  RGB color = palette[0];
  for (uint_fast8_t i=0; i<palette_len; i++) {
    RGB p = palette[i];
    
    int r_dist = c.r - p.r;
    int g_dist = c.g - p.g;
    int b_dist = c.b - p.b;
    
    uint32_t dist = r_dist*r_dist + g_dist*g_dist + b_dist*b_dist;
    if (dist < min_dist) {
      color = p;
      min_dist = dist;
    }
  }
  
  return color;
}

static inline uint8_t ClampU8(int x) {
  if (x > 255) x = 255;
  if (x < 0) x = 0;
  return x;
}

static inline RGB FloydSteinbergApply(uint_fast8_t n, int_fast16_t err[3], RGB c) {
  RGB result = {
    .r = ClampU8(c.r + ((err[0]*n) >> 4)),
    .g = ClampU8(c.g + ((err[1]*n) >> 4)),
    .b = ClampU8(c.b + ((err[2]*n) >> 4)),
    .a = 255
  };
  return result; 
}

static void __attribute__ ((noinline)) FloydSteinberg(void) {
  for (size_t y=0; y<img_h; y++) {
    RGB *row = img + y*img_w;
    RGB *row_next = row + img_w;
    for (size_t x=0; x<img_w; x++) {
      RGB orig = row[x];
      RGB c = ChooseColorRGB(orig);
      c.a = 255;
      int_fast16_t err[3] = {
        (int) orig.r - c.r,
        (int) orig.g - c.g,
        (int) orig.b - c.b,
      };

      row[x]        = c;
      if (x+1 < img_w)
        row[x+1]      = FloydSteinbergApply(7, err, row[x+1]);

      if (y+1 < img_h) {
        row_next[x]   = FloydSteinbergApply(5, err, row_next[x]);
        if (x > 0)
          row_next[x-1] = FloydSteinbergApply(3, err, row_next[x-1]);
        if (x+1 < img_w)
          row_next[x+1] = FloydSteinbergApply(1, err, row_next[x+1]);
      }
    }
  }
}

int main(int argc, char **argv) {
  HandleArguments(argc, argv); 
  FloydSteinberg();
  /* TODO: error checking */
  fpng::fpng_encode_image_to_file(output_filename, img, img_w, img_h, 4);
  free(img);
}
