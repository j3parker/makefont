#include "png.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H

FT_Library ft_lib;
FT_Face ft_face;

typedef uint8_t u8;
typedef uint32_t u32;
typedef float f32;
typedef double f64;

#define alphabetLen (size_t)(sizeof(alphabet) - 1)
char alphabet[] = "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";

typedef struct {
  char c;
  u32 w, h;
  u32 s0, t0, s1, t1;
} rGlyph_t;

rGlyph_t glyphs[alphabetLen];

int max(int a, int b) { return a > b ? a : b; }

f32 efficiency;

u32 texSizeRef;
u32 texPadding;

u32 renderFlags = FT_RENDER_MODE_NORMAL;

void setFontSize(u32 fontSize) {
  FT_Matrix matrix = { (int)((1.0/64.0) * 0x10000L),
                       (int)((0.0)      * 0x10000L),
                       (int)((0.0)      * 0x10000L),
                       (int)((1.0)      * 0x10000L) }; 
  assert(!FT_Set_Char_Size(ft_face, fontSize*64, 0, 72*64, 72));
  FT_Set_Transform(ft_face, &matrix, NULL);
}

/**
 * Actually renders the atlas
 */
void makeTexture(u32 fontSize, u32 texSize, u8* tex) {
  setFontSize(fontSize);
  memset(tex, 0, texSize*texSize*4);
  for(size_t i = 0; i < texSize*texSize; i++) {
    tex[i*4 + 0] = 255;
    tex[i*4 + 1] = 255;
    tex[i*4 + 2] = 255;
    tex[i*4 + 3] = 255;
  }
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
      for(u32 j = 0; j < w; j++) {
        u8 color = 255-buf[j + i*p];
        tex[(px + j)*4 + (py + i)*texSize*4 + 0] = color;
        tex[(px + j)*4 + (py + i)*texSize*4 + 1] = color;
        tex[(px + j)*4 + (py + i)*texSize*4 + 2] = color;
      }
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

    maxHeight = max(maxHeight, h);

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

void printGlyphs() {
  for(size_t index = 0; index < alphabetLen; index++) {
    printf("%c: W=%d H=%d\n", glyphs[index].c, glyphs[index].w, glyphs[index].h);
  }
}

int main(int argc, char **argv) {
  if(argc != 4) {
    printf("Usage: makefont <texture size> <glyph padding> <font file>\n");
    exit(1);
  }
  texSizeRef = atoi(argv[1]);
  texPadding = atoi(argv[2]);
  char* name = argv[3];
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
  printf("Finding packing for a reference texture of size %dx%d...\n", texSizeRef, texSizeRef);
  int fontSize =  findMax(binPack, texSizeRef);
  printf("Packing found. Efficiency = %f%%, with font size %d\n\n\n", 100.0*(f64)efficiency/(f64)texSizeRef/(f64)texSizeRef, fontSize);

  assert(findFontSize(fontSize, texSizeRef));  
  assert(!findFontSize(fontSize + 1, texSizeRef));

  /**
   * STAGE 2
   *
   * Generate textures, find optimal font size for each
   */
  u32 texSize = texSizeRef; 
  while(1) {
    u32 fontSize = findMax(findFontSize, texSize);
    if(fontSize < 5) break;
    u8 *tex = malloc(texSize*texSize*4);
    printf("Best font size for %dx%d: %d\n", texSize, texSize, fontSize);
    makeTexture(fontSize, texSize, tex);
    snprintf(filename, 128, "%s_%d.png", name, texSize);
    lodepng_encode32_file(filename, tex, texSize, texSize);
    printf("Saved %s.\n\n", filename);
    free(tex);
    texSize /= 2;
  }

  FT_Done_FreeType(ft_lib);
}
