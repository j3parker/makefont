#include "png.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H
#include FT_LCD_FILTER_H

FT_Library ft_lib;
FT_Face ft_face;

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t i32;
typedef float f32;
typedef double f64;

#define alphabetLen (size_t)(sizeof(alphabet) - 1)
char alphabet[] = "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";

typedef struct {
  char c;
  u32 w, h;
  u32 s0, t0, s1, t1;
  f64 offsetx, offsety;
  f64 advancex, advancey;
} rGlyph_t;

rGlyph_t glyphs[alphabetLen];

u32 u32max(u32 a, u32 b) { return a > b ? a : b; }
u32 u32min(u32 a, u32 b) { return a > b ? b : a; }
i32 i32min(i32 a, i32 b) { return a > b ? b : a; }
i32 i32max(i32 a, i32 b) { return a > b ? a : b; }
f64 f64cap(f64 low, f64 high, f64 x) { if(x < low) x = low; if(x > high) x = high; return x; }
#define f64min fminf

f32 efficiency;

u32 texSizeRef;
u32 texPadding;

u32 renderFlags = FT_RENDER_MODE_NORMAL;

u32 numTex = 0;

void setFontSize(u32 fontSize) {
  FT_Matrix matrix = { (int)((1.0/64.0) * 0x10000L),
                       (int)((0.0)      * 0x10000L),
                       (int)((0.0)      * 0x10000L),
                       (int)((1.0)      * 0x10000L) }; 
  assert(!FT_Set_Char_Size(ft_face, fontSize*64, 0, 72*64, 72));
  FT_Set_Transform(ft_face, &matrix, NULL);
}

/**
 * Compute signed distance field for srcTex (1 channel) and store in dstTex.
 * The channels for dstTex are: 
 * red   = SDF
 * green = top right/bottom left SDF
 * blue  = alpha (anti-aliasing)
 */
void makeSDFTexture(u8* dstTex, u8* srcTex, u32 winSize, u32 texSize) {
  memset(dstTex, 0, texSize*texSize*3);
  assert(srcTex != NULL);
  assert(dstTex != NULL);
  // 255 means glyph pixel, 0 means no glyph
  for(i32 sy = 0; sy < (i32)texSize; sy++) {
    for(i32 sx = 0; sx < (i32)texSize; sx++) {
      u32 minSqrDist = (winSize + 1)*(winSize + 1);

      u8 scolor = srcTex[sx + sy*texSize];

      for(i32 py = i32max(0, sy - winSize); py < i32min(texSize, sy + winSize); py++) {
        for(i32 px = i32max(0, sx - winSize); px < i32min(texSize, sx + winSize); px++) {
          assert(px >= 0 && px < (i32)texSize);
          assert(py >= 0 && py < (i32)texSize);
          if(py == sy && px == sx) continue;
          u8 pcolor = srcTex[px + py*texSize];
          if((!scolor) != (!pcolor)) { // one is black and the other is not black
            i32 dist = (sx - px)*(sx - px) + (sy - py)*(sy - py);
            minSqrDist = u32min(minSqrDist, dist);
          }
        }
      }

      f64 minDist = sqrt((f64)minSqrDist);

      // [0, winSize + 1] --> [0, 1]
      f64 dist = f64cap(0.0, 1.0, minDist/(f64)winSize);

      u8 tcolor;
      if(scolor) {
        tcolor = (u8)round(127.0 + dist*128.0);
      } else {
        tcolor = (u8)round((1.0 - dist)*127.0);
      }

      dstTex[(sx + sy*texSize)*3 + 0] = tcolor;
      dstTex[(sx + sy*texSize)*3 + 1] = tcolor;

      dstTex[(sx + sy*texSize)*3 + 2] = scolor;
    }
  }
}

/**
 * Renders the atlas
 */
void makeTexture(u32 fontSize, u32 texSize, u8* tex) {
  assert(tex != NULL);
  setFontSize(fontSize);
  memset(tex, 0, texSize*texSize);
  for(size_t index = 0; index < alphabetLen; index++) {
    int glyph_index = FT_Get_Char_Index(ft_face, glyphs[index].c);
    assert(glyph_index);
    assert(!FT_Load_Glyph(ft_face, glyph_index, 0));
    assert(!FT_Render_Glyph(ft_face->glyph, renderFlags));
    
    assert(glyphs[index].s1 >= glyphs[index].s0);
    assert(glyphs[index].t1 >= glyphs[index].t0);

    u8* buf = ft_face->glyph->bitmap.buffer;
    u32 w = ft_face->glyph->bitmap.width;
    u32 h = ft_face->glyph->bitmap.rows;
    u32 p = ft_face->glyph->bitmap.pitch;

    u32 px = ceilf((f32)glyphs[index].s0/(f32)texSizeRef*(f32)texSize);
    u32 py = ceilf((f32)glyphs[index].t0/(f32)texSizeRef*(f32)texSize);

    assert(px + w < texSize);
    assert(py + h < texSize);
    
    for(u32 i = 0; i < h; i++) {
      memcpy(&tex[px + (py + i)*texSize], &buf[i*p], w);
    }
  }
}

/**
 * Determines if a font size will fit into the bins
 * at this texture size (lol so lazy)
 */
bool findFontSize(u32 fontSize, u32 texSize) {
  setFontSize(fontSize);
  for(size_t index = 0; index < alphabetLen; index++) {
    int glyph_index = FT_Get_Char_Index(ft_face, glyphs[index].c);
    assert(glyph_index);
    assert(!FT_Load_Glyph(ft_face, glyph_index, 0));
    assert(!FT_Render_Glyph(ft_face->glyph, renderFlags));

    assert(glyphs[index].s1 >= glyphs[index].s0);
    assert(glyphs[index].t1 >= glyphs[index].t0);

    u32 w = ceilf((f64)(glyphs[index].s1 - glyphs[index].s0)/(f64)texSizeRef*(f64)texSize);
    u32 h = ceilf((f64)(glyphs[index].t1 - glyphs[index].t0)/(f64)texSizeRef*(f64)texSize);
    if(w < (u32)ft_face->glyph->bitmap.width ||
       h < (u32)ft_face->glyph->bitmap.rows) {
      return false;
    }
  }

  return true;
}

/**
 * For qsort
 */
int compare_glyph(const void* _g1, const void* _g2) {
  const u32 h1 = ((const rGlyph_t*)_g1)->h;
  const u32 h2 = ((const rGlyph_t*)_g2)->h;
  return h1 - h2;
}

/**
 * Determines if there is a packing of the alphabet at size fontSize
 * into a texSize X texSize texture.
 */
bool binPack(u32 fontSize, u32 texSize) {
  // First, get glyph metrics
  setFontSize(fontSize);
  for(size_t index = 0; index < alphabetLen; index++) {
    int glyph_index = FT_Get_Char_Index(ft_face, glyphs[index].c);
    assert(glyph_index);
    assert(!FT_Load_Glyph(ft_face, glyph_index, 0));
    assert(!FT_Render_Glyph(ft_face->glyph, renderFlags));

    glyphs[index].w = ft_face->glyph->bitmap.width;
    glyphs[index].h = ft_face->glyph->bitmap.rows;
  }
  
  // Next, sort glyphs based on heights
  qsort(&glyphs[0], alphabetLen, sizeof(rGlyph_t), compare_glyph);

  // Finally, pack
  efficiency = 0;
  u32 cx = texPadding;
  u32 cy = texPadding;
  u32 maxHeight = 0;
  bool foundOneGlyph = false;
  setFontSize(fontSize);
  for(size_t index = 0; index < alphabetLen; index++) {
    int glyph_index = FT_Get_Char_Index(ft_face, glyphs[index].c);
    assert(glyph_index);
    assert(!FT_Load_Glyph(ft_face, glyph_index, 0));
    assert(!FT_Render_Glyph(ft_face->glyph, renderFlags));

    u32 w = ft_face->glyph->bitmap.width;
    u32 h = ft_face->glyph->bitmap.rows;

    glyphs[index].advancex = ft_face->glyph->advance.x/64.0;
    glyphs[index].advancey = ft_face->glyph->advance.y/64.0;
    glyphs[index].offsetx = ft_face->glyph->bitmap_left;
    glyphs[index].offsety = ft_face->glyph->bitmap_top;

    if(w >= texSize) return false; // unlikely but necessary check

    if(cx + w >= texSize) {
      cx = texPadding;
      cy += maxHeight + texPadding;
      maxHeight = 0;
    }

    if(cy + h >= texSize) return false;

    assert(cx + w < texSize);
    assert(cy + h < texSize);

    glyphs[index].s0 = cx;
    glyphs[index].t0 = cy;

    cx += w;

    glyphs[index].s1 = cx;
    glyphs[index].t1 = cy + h;

    cx += texPadding;

    maxHeight = u32max(maxHeight, h);

    // debug stuff
    efficiency += (glyphs[index].s1 - glyphs[index].s0)*(glyphs[index].t1 - glyphs[index].t0);
    assert(cx <= texSize + texPadding);
    assert(cy <= texSize + texPadding);
    foundOneGlyph = true;
  }
  assert(foundOneGlyph);

  return true;
}

/**
 * Returns a u32 which is the maximum u32 that func will accept
 * with texSize as its second argument (assumes that if
 * func(x, texSize) is false, then func(y, texSize) is false for 
 * all y >=x)
 */
u32 findMax(bool (*func)(u32, u32), u32 texSize) {
  u32 guess = 1;
  while(func(guess, texSize)) {
    guess *= 2;
  }
  if(guess == 1) return 0;
  u32 low = guess / 2;
  u32 high = guess;
  while(low < high) {
    u32 mid = (low + high) / 2;
    if(func(mid, texSize)) {
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }
  guess = high;
  if(!func(guess, texSize)) guess--;
  assert(func(guess, texSize));
  return guess;
}

void printMetrics() {
  printf("max=%d\n", texSizeRef);
  printf("numTex=%d\n", numTex);
  printf("numChars=%d\n", (i32)alphabetLen);
  for(size_t index = 0; index < alphabetLen; index++) {
    printf("%c: s0=%f t0=%f s1=%f t1=%f advancex=%f advancey=%f offsetx=%f offsety=%f\n",
           glyphs[index].c,
           (f64)glyphs[index].s0/(f64)texSizeRef,
           (f64)glyphs[index].t0/(f64)texSizeRef,
           (f64)glyphs[index].s1/(f64)texSizeRef,
           (f64)glyphs[index].t1/(f64)texSizeRef,
           glyphs[index].advancex/(f64)texSizeRef,
           glyphs[index].advancey/(f64)texSizeRef,
           glyphs[index].offsetx/(f64)texSizeRef,
           glyphs[index].offsety/(f64)texSizeRef
           );
  }
}

int main(int argc, char **argv) {
  if(argc != 5) {
    fprintf(stderr, "Usage: makefont <texture size> <glyph padding> <SDF window size> <font file>\n");
    exit(1);
  }
  texSizeRef = atoi(argv[1]);
  texPadding = atoi(argv[2]);
  u32 sdfWin = atoi(argv[3]);
  char* name = argv[4];
  char filename[128];
  snprintf(filename, 128, "%s.ttf", name);

  assert(!FT_Init_FreeType(&ft_lib));
  assert(!FT_New_Face(ft_lib, filename, 0, &ft_face));

  for(size_t index = 0; index < alphabetLen; index++) {
    glyphs[index].c = alphabet[index];
  }

  /**
   * STAGE 1
   *
   * Find the best font size for desired texture size and
   * figure out the bin locations
   */
  fprintf(stderr, "Finding packing for a reference texture of size %dx%d...\n", texSizeRef, texSizeRef);
  int fontSize =  findMax(binPack, texSizeRef);
  fprintf(stderr, "Packing found. Efficiency = %f%%, with font size %d\n\n\n", 100.0*(f64)efficiency/(f64)texSizeRef/(f64)texSizeRef, fontSize);

  assert(findFontSize(fontSize, texSizeRef));  
  assert(!findFontSize(fontSize + 1, texSizeRef));

  /**
   * STAGE 2
   *
   * Generate textures, find optimal font size for each
   */
  u32 texSize = texSizeRef; 
  while(1) {
    // Find best font size to fit the bins for this texture size
    u32 fontSize = findMax(findFontSize, texSize);
    fprintf(stderr, "Best font size for %dx%d: %d\n", texSize, texSize, fontSize);

    // Fonts smaller than this look bad anyway XXX user configurable?
    if(fontSize < 5) break;

    u8 *texGrey = malloc(texSize*texSize);
    u8 *texSDF = malloc(texSize*texSize*3);

    makeTexture(fontSize, texSize, texGrey);
    makeSDFTexture(texSDF, texGrey, (f64)sdfWin/(f64)texSizeRef*(f64)texSize, texSize);

    snprintf(filename, 128, "%s_%d.png", name, texSize);
    lodepng_encode24_file(filename, texSDF, texSize, texSize);
    numTex++;
    fprintf(stderr, "Saved %s.\n\n", filename);

    free(texGrey);
    free(texSDF);

    texSize /= 2;
  }

  printMetrics();

  FT_Done_FreeType(ft_lib);
}
