/*
  mkfnt.c

  This utility processes a textual definition of a
  512-char, 8x8 bitmap, monospace font and generates
  the various derivative binary and text files with
  font data and metadata to include in your projects.

  Public Domain

  How to compile, e.g.:
    $ gcc -std=c99 -O2 -Wall -Wextra -pedantic mkfnt.c -o mkfnt

  How to use, e.g.:
    $ ./mkfnt 512_8_bold.txt
*/
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define PRINT_SHAPES 0
#define PRINT_BUCKET_COUNTS 0

typedef unsigned char uchar;
typedef unsigned uint;
typedef unsigned long ulong;

#define MAXH 1024 // input file max vertical dimension
#define MAXW 256  // input file max horizontal dimension
char img[MAXH][MAXW];
char img2[MAXH][MAXW];

#define MAXC 512 // max num of chars in font
int cpos[MAXC][2]; // [][0]=x, [][1]=y of left & top of char in img/img2[][]
int has_cp[MAXC]; // whether char has an assigned code point

#define MAXT (MAXC*2) // max num of code points in font
int tab_cnt;
ulong tab[MAXT][2]; // [][0]=code point, [][1]=index into font

int chars_found;
FILE *fin, *ffnt, *fimg, *ftab, *fhashtab, *fmi, *fvh, *fh, *fbfm, *fbfm2, *fpan;

int bit(int c)
{
  switch (c)
  {
  case '.': return 0;
  case 'O': return 1;
  default: return -1;
  }
}

void process(int x, int y)
{
  int i = 0, j = 0;

  while (bit(img[y][x + i]) >= 0)
    i++;
  while (bit(img[y + j][x]) >= 0)
    j++;

  if (i == 8 && j == 8 && chars_found < MAXC)
  {
    for (j = 0; j < 8; j++)
    {
      uint b = 0;
      for (i = 0; i < 8; i++)
      {
        int bt = bit(img[y + j][x + i]);
#if PRINT_SHAPES
        putchar(img[y + j][x + i]);
#endif
        assert(bt >= 0); // expecting '.' or 'O' in the 8x8 cell
        b = b * 2 + bt;
        img2[y + j][x + i] = img[y + j][x + i];
        img[y + j][x + i] = 0;
      }
#if PRINT_SHAPES
      putchar('\n');
#endif
    }
#if PRINT_SHAPES
    putchar('\n');
#endif
    cpos[chars_found][0] = x;
    cpos[chars_found++][1] = y;
  }
}

int tcmp(const void* va, const void* vb)
{
  uint a = *(const uint*)va;
  uint b = *(const uint*)vb;

  if (a < b)
    return -1;
  if (a > b)
    return +1;
  return 0;
}

void cp2utf8(void* buf, ulong cdpt)
{
  uchar* p = buf;

  if (cdpt < 0x800)
  {
    if (cdpt < 0x80)
    {
      *p++ = cdpt;
    }
    else
    {
      *p++ = (cdpt >> 6) | 0xC0;
      *p++ = (cdpt & 0x3F) | 0x80;
    }
  }
  else
  {
    if (cdpt < 0x10000)
    {
      *p++ = (cdpt >> 12) | 0xE0;
      *p++ = ((cdpt >> 6) & 0x3F) | 0x80;
      *p++ = (cdpt & 0x3F) | 0x80;
    }
    else
    {
      assert(cdpt < 0x10FFFF);
      *p++ = (cdpt >> 18) | 0xF0;
      *p++ = ((cdpt >> 12) & 0x3F) | 0x80;
      *p++ = ((cdpt >> 6) & 0x3F) | 0x80;
      *p++ = (cdpt & 0x3F) | 0x80;
    }
  }
  *p = '\0';
}

ulong utf8_2cp(const void* buf, size_t sz, size_t* sz_consumed)
{
  const uchar* p = buf;

  if (sz < 1)
    goto lerr;

  if (p[0] < 0x80)
  {
    *sz_consumed = 1;
    return p[0];
  }
  else if (p[0] >> 5 == 6)
  {
    if (sz < 2 || p[1] >> 6 != 2)
      goto lerr;
    *sz_consumed = 2;
    return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
  }
  else if (p[0] >> 4 == 0xE)
  {
    if (sz < 3 || p[1] >> 6 != 2 || p[2] >> 6 != 2)
      goto lerr;
    *sz_consumed = 3;
    return ((p[0] & 0xFU) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
  }
  else
  {
    ulong cp;
    if (sz < 4 || p[0] >> 3 != 0x1E ||
        p[1] >> 6 != 2 || p[2] >> 6 != 2 || p[3] >> 6 != 2)
      goto lerr;
    cp = ((p[0] & 7UL) << 18) | ((p[1] & 0x3F) << 12) |
         ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    if (cp > 0x10FFFF)
      goto lerr;
    *sz_consumed = 4;
    return cp;
  }

lerr:
  *sz_consumed = 0;
  return 0xFFFD;
}

void mktab(void)
{
  int c;
  int cstart, cstop = 0, pair_idx;
  uint pair;
  int uFFFDlast;

  for (c = 0; c < chars_found; c++)
  {
    int x = cpos[c][0];
    int y = cpos[c][1];
    int tab_start, tab_stop, t;
    ulong idx;

    assert(y >= 3); // must have font index and code point numbers above char cell

    y--;
    assert(img[y][x] == ' ' || img[y][x] == '\0'); // no vertical padding between code point and char cell
    while (img[y][x] == ' ' || img[y][x] == '\0')
    {
      assert(y > 0);
      y--;
    }

    assert(img[y][x] == 'U' && img[y][x + 1] == '+'); // no code point number
    tab_start = tab_cnt;
    tab_stop = -1;
    while (img[y][x] == 'U' && img[y][x + 1] == '+')
    {
      ulong cp;
      errno = 0;
      cp = strtoul(&img[y][x + 2], NULL, 16);
      assert(errno == 0); // conversion error
      assert(cp <= 0xFFFF); // code point doesn't fit in 16 bits

      if (isxdigit(img[y][x + 2])) // skip "U+????"
      {
        tab_stop = tab_cnt;
        assert(tab_cnt < MAXT);
        tab[tab_cnt++][0] = cp;
      }

      assert(y > 0);
      y--;
    }

    if (tab_stop >= 0) // skip "U+????"
      assert(isxdigit(img[y][x])); // no font index
    errno = 0;
    idx = strtoul(&img[y][x], NULL, 16);
    assert(errno == 0); // conversion error
    assert(idx < MAXC); // index beyond font's end
    for (t = tab_start; t <= tab_stop; t++) // tab_stop < 0 skips "U+????"
    {
      // tab[t][0] has been set above
      tab[t][1] = idx;
      has_cp[idx] = 1;
    }
  }

  // sort by code point for binary search conversion
  // from code point to font index
  qsort(tab, tab_cnt, sizeof tab[0], tcmp);
  uFFFDlast = tab[tab_cnt - 1][0] == 0xFFFD; // U+FFFD must be last
  assert(tab[tab_cnt - 1 - uFFFDlast][0] <= 0x7FFF); // others must be <= 0x7FFF

  printf("tab_cnt: %d\n", tab_cnt);

  cstart = tab_cnt;
  fprintf(ftab, "#ifndef DXPAIR\n");
  for (c = 0; c < tab_cnt - uFFFDlast; c++)
  {
    const char* comma = (c < tab_cnt - 1 - uFFFDlast) ? "," : "";
    char comment[6] = { '\0' };
    uint cdpt = tab[c][0];
    uint fidx = tab[c][1];
    // we want cpfi to have cdpt and fidx's bit 8 in cpfi's bit 15
    // so that cdpt and fidx can be stored as two tables of ~600 elements
    // each: 16-bit cpfi and 8-bit dx:
    uint cpfi = cdpt | ((fidx & 0x100) << 7);
    uint dx = fidx & 0xFF;

    if (c > 0)
      assert(tab[c - 1][0] < cdpt);
    if (cdpt < 128 || cdpt >= 0x8000)
      continue; // skip ASCII subrange and code points >= 0x8000
    if (cstart >= tab_cnt)
      cstart = c;
    cstop = c;
    cp2utf8(comment, cdpt);
    if (cdpt == '\\')
      strcpy(comment, "'\\\\'"); // avoid preprocessor line concatenation
    fprintf(ftab,
            "CDPT(0x%04X) FIDX(0x%03X) CPFI(0x%04X) DX(0x%02X)%s // %s\n",
            cdpt, fidx, cpfi, dx, comma, comment);
  }
  fprintf(ftab, "#else // else of ifndef DXPAIR\n");
  pair = pair_idx = 0;
  for (c = cstart; c <= cstop; c++)
  {
    uint fidx = tab[c][1];
    uint dx = fidx & 0xFF;

    if (!pair_idx)
      pair = dx;
    else
    {
      pair |= dx << 8;
      fprintf(ftab, "DXPAIR(0x%04X)%s\n", pair, ((c + 1 < cstop) ? "," : ""));
    }
    pair_idx ^= 1;
  }
  if (pair_idx)
    fprintf(ftab, "DXPAIR(0x%04X)\n", pair);
  fprintf(ftab, "#endif // endif of ifndef DXPAIR\n");
}

void mkhashtab(void)
{
  uint bucket[256] = { 0 };
  uint slots = 0;
  int c;
  size_t hidx, htsz;
  uint *ht = NULL;

  for (c = 0; c < tab_cnt; c++)
  {
    uint cdpt = tab[c][0];
    if (cdpt < 128 || cdpt >= 0x8000)
      continue; // skip ASCII subrange and code points >= 0x8000
/*
  Possible hash table implementation:
  - 256 buckets, indexed by 8 least significant bits of code point
    - each bucket has 6 slots (the maximum number of code points
      hashing into the same bucket)
      - each slot contains 16 bits: 
        - 7 most significant bits of code point (code points are 15-bit)
        - 9 bits of font index
      - unused slots are filled with 0xFFFF
  - size: 256*6*2=3072 bytes
    OTOH, binary search's array size: (690-96)*3=1782 bytes
    (*3: 15-bit code point + 9-bit font index)
*/
#define HASH(X) ((X) % 256)
    bucket[HASH(cdpt)]++;
    if (slots < bucket[HASH(cdpt)])
      slots = bucket[HASH(cdpt)];
  }
  printf("hash table slots per bucket: %d\n", slots);
#if PRINT_BUCKET_COUNTS
  for (c = 0; c < 256; c += 4)
  {
    int d;
    for (d = 0; d < 4; d++)
      printf("bkt%02X = %2u%c ",
             c + d, bucket[c + d], ((bucket[c + d] >= 5) ? '*' : ' '));
    putchar('\n');
  }
#endif

  htsz = sizeof *ht * 256 * slots;
  ht = malloc(htsz);
  assert(ht);
  for (hidx = 0; hidx < 256 * slots; hidx++)
    ht[hidx] = 0xFFFF;
  for (c = 0; c < tab_cnt; c++)
  {
    uint cdpt = tab[c][0];
    uint fidx = tab[c][1];
    uint bucket = cdpt % 256;
    uint slot;

    if (cdpt < 128 || cdpt == 0xFFFD)
      continue; // skip ASCII subrange and code points >= 0x8000
    assert(cdpt < 0x8000);
    for (slot = 0; slot < slots; slot++)
    {
      hidx = bucket * slots + slot;
      if (ht[hidx] == 0xFFFF)
      {
        ht[hidx] = (cdpt & 0x7F00) | (fidx & 0xFF) | ((fidx & 0x100) << 7);
        break;
      }
    }
  }

  fprintf(fhashtab, "// 256 buckets, %u slots per bucket\n", slots);
  for (hidx = 0; hidx < 256 * slots; hidx++)
  {
    const char* comma = (hidx < 256 * slots - 1) ? "," : "";
    char comment[6] = { '\0' };

    if (ht[hidx] != 0xFFFF)
    {
      uint cdpt = (ht[hidx] & 0x7F00) | (hidx / slots);
      cp2utf8(comment, cdpt);
      if (cdpt == '\\')
        strcpy(comment, "'\\\\'"); // avoid preprocessor line concatenation
      fprintf(fhashtab, "HTVAL(0x%04X)%s // 0x%04X=%s\n",
              ht[hidx], comma, cdpt, comment);
    }
    else
    {
      fprintf(fhashtab, "HTVAL(0x%04X)%s\n",
              ht[hidx], comma);
    }
  }

  free(ht);
}

void mkimg(void)
{
  int imgc = (chars_found + 15) / 16 * 16;
  int imgw = 16 * 8;
  int imgh = imgc / 16 * 8;
  int x, y;

  fprintf(fimg,
          "P5\n#\n%d %d\n1\n",
          imgw,
          imgh);

  for (y = 0; y < imgh; y++)
    for (x = 0; x < imgw; x++)
    {
      int c = (y / 8) * 16 + (x / 8);
      int pix = (c < chars_found)
                ? bit(img2[cpos[c][1] + y % 8][cpos[c][0] + x % 8])
                : 0;
      fputc(pix, fimg);
    }
}

void mkfnt(void)
{
  int first = 1;
  int c;

  fprintf(fmi,
          "#File_format=Hex\n#Address_depth=%d\n#Data_width=8\n",
          chars_found * 8);

  for (c = 0; c < chars_found; c++)
  {
    int x = cpos[c][0];
    int y = cpos[c][1];
    int i, j;

#if 0
    if (!has_cp[c])
      continue; // skip chars w/o assigned code points (U+????)
#endif

    if (!first)
    {
      fprintf(fh, ",\n");
      fprintf(fvh, ",\n");
    }
    first = 0;

    for (j = 0; j < 8; j++)
    {
      uchar b = 0;
      for (i = 0; i < 8; i++)
        b = b * 2 + bit(img2[y + j][x + i]);

      fputc(b, ffnt);
      fprintf(fmi, "%02X\n", b);
      fprintf(fh, "0x%02X", b);
      fprintf(fvh, "8'h%02X", b);
      if (j < 7)
      {
        fputc(',', fh);
        fputc(',', fvh);
      }
    }
  }

  fputc('\n', fh);
  fputc('\n', fvh);
}

// Import data for https://www.pentacom.jp/pentacom/bitfontmaker2/
// (the page can make a TTF font file from the font)
void mkbfm(void)
{
  int c;

  for (c = 0; c < tab_cnt; c++)
  {
    uint cdpt = tab[c][0];
    uint fidx = tab[c][1];
    uint i, j;

    // 8x8
    fprintf(fbfm, "\"%u\":[0,0,0,0,0,", cdpt);
    // Doubled to 8x16
    fprintf(fbfm2, "\"%u\":[", cdpt);

    for (j = 0; j < 8; j++)
    {
      uint val = 0;
      for (i = 0; i < 8; i++)
        val |= bit(img2[cpos[fidx][1] + j][cpos[fidx][0] + i]) << i;
      val <<= 2;

      // 8x8
      fprintf(fbfm, "%u,", val);
      // Doubled to 8x16
      fprintf(fbfm2, "%u,%u", val, val);

      if (j < 7)
        fputc(',', fbfm2);
    }
    // 8x8
    fprintf(fbfm, "0,0,0],\n");
    // Doubled to 8x16
    fprintf(fbfm2, "],\n");
  }
}

void mkpan(void)
{
  static const char utf8text[] =
  "https://en.wikipedia.org/wiki/Pangram\n"
  "https://backpacker.gr/pangrams/\n"
  "\n"
  "Czech: Příliš žluťoučký kůň úpěl ďábelské ódy.\n"
  "Danish: Høj bly gom vandt fræk sexquiz på wc.\n"
  "Dutch: Pa's wĳze lynx bezag vroom het fikse aquaduct.\n"
  "English: The quick brown fox jumps over the lazy dog.\n"
  "Finnish: Wieniläinen siouxia puhuva ökyzombi diggaa Åsan roquefort-tacoja.\n"
  "German: Victor jagt zwölf Boxkämpfer quer über den großen Sylter Deich.\n"
  "Hungarian: Egy hűtlen vejét fülöncsípő, dühös mexikói úr ázik Quitóban.\n"
  "Icelandic: Kæmi ný öxi hér, ykist þjófum nú bæði víl og ádrepa.\n"
  "Polish: Pchnąć w tę łódź jeża lub ośm skrzyń fig.\n"
  "Portuguese: Ré só que vê galã sexy pôr kiwi talhado à força em baú põe\n"
  "            juíza má em pânico.\n"
  "Romanian: Încă vând gem, whisky bej și tequila roz, preț fix.\n"
  "Serbian: Ljubazni fenjerdžija čađavog lica hoće da mi pokaže štos.\n"
  "Slovak: Kŕdeľ šťastných ďatľov učí pri ústí Váhu mĺkveho koňa obhrýzať kôru\n"
  "        a žrať čerstvé mäso.\n"
  "Turkish: Pijamalı hasta yağız şoföre çabucak güvendi.\n"
  "\n"
  "Greek: Φλεγματικά χρώματα που εξοβελίζουν ψευδαισθήσεις.\n"
  "\n"
  "Belarusian: У Іўі худы жвавы чорт у зялёнай камізэльцы пабег пад’есці фаршу\n"
  "            з юшкай.\n"
  "Bulgarian: Под южно дърво, цъфтящо в синьо, бягаше малко пухкаво зайче.\n"
  "Russian: Съешь ещё этих мягких французских булок, да выпей же чаю.\n"
  "Serbian: Љубазни фењерџија чађавог лица хоће да ми покаже штос.\n"
  "\n"
  "Hebrew: ידו ,הסח םעטב רזג תצק לכא ןפש\n"
  ;
  static const uint attrs15[15] =
  {
    0x06, 0x07, 0x0B, 0x0E, 0x0F,
    0x17, 0x1E, 0x1F,
    0x4E, 0x4F,
    0x5F,
    0x60,
    0x70, 0x71, 0x79
  };
  static const uchar pal4bpp[16][3] =
  {
    { 0x00, 0x00, 0x00 }, // black
    { 0x00, 0x00, 0xAA }, // blue
    { 0x00, 0xAA, 0x00 }, // green
    { 0x00, 0xAA, 0xAA }, // cyan
    { 0xAA, 0x00, 0x00 }, // red
    { 0xAA, 0x00, 0xAA }, // magenta
    { 0xAA, /*0x55*/0xAA, 0x00 }, // /*brown*/ -> yellow
    { 0xAA, 0xAA, 0xAA }, // bright gray
    { 0x55, 0x55, 0x55 }, // dark gray
    { 0x00, 0x00, 0xFF }, // bright blue
    { 0x00, 0xFF, 0x00 }, // bright green
    { 0x00, 0xFF, 0xFF }, // bright cyan
    { 0xFF, 0x00, 0x00 }, // bright red
    { 0xFF, 0x00, 0xFF }, // bright magenta
    { 0xFF, 0xFF, 0x00 }, // yellow
    { 0xFF, 0xFF, 0xFF }  // white
  };

  int maxw = 0, txtw = 0, txth = 0;
  enum { charh = /*8 or 16*/16 };
  int imgw, imgh;
  const char* p = utf8text;
  size_t len = strlen(utf8text), sz = len, sz_consumed;
  ulong cp;
  ulong* utf32text;
  int x, y, i, j, fidx;

  // Find text dimensions...
  while (sz)
  {
    cp = utf8_2cp(p, sz, &sz_consumed);
    if (cp == '\n')
    {
      if (maxw < txtw)
        maxw = txtw;
      txtw = 0;
      txth++;
    }
    else
    {
      txtw++;
    }
    p += sz_consumed;
    sz -= sz_consumed;
  }

  txtw = maxw;
  imgw = txtw * 8;
  imgh = txth * charh; // will double char height to 8x16
  fprintf(fpan, "P6\n#\n%d %d\n255\n", imgw, imgh);

  // To simplify things, put the text as UTF-32 into a rectangular buffer.
  sz = txtw * txth * sizeof *utf32text;
  utf32text = calloc(sz, 1);
  assert(utf32text);
  p = utf8text;
  sz = len;
  y = x = 0;
  while (sz)
  {
    cp = utf8_2cp(p, sz, &sz_consumed);
    if (cp == '\n')
    {
      x = 0;
      y++;
    }
    else
    {
      utf32text[y * txtw + x] = cp;
      x++;
    }
    p += sz_consumed;
    sz -= sz_consumed;
  }

  for (y = 0; y < txth; y++)
  {
    uint attr = attrs15[y % 15];
    for (j = 0; j < charh; j++)
      for (x = 0; x < txtw; x++)
      {
        ulong (*pcdptfidx)[2];
        cp = utf32text[y * txtw + x];
        pcdptfidx = bsearch(&cp, tab, tab_cnt, sizeof tab[0], tcmp);
        if (!pcdptfidx)
          pcdptfidx = (tab[tab_cnt - 1][0] == 0xFFFD)
                    ? &tab[tab_cnt - 1]
                    : &tab[0];
        fidx = tab[pcdptfidx - tab][1];
        for (i = 0; i < 8; i++)
        {
          int c;
          int pix = bit(img2[cpos[fidx][1] + j * 8 / charh][cpos[fidx][0] + i]);
          pix = pix ? attr % 16 : attr / 16;
          for (c = 0; c < 3; c++)
            fputc(pal4bpp[pix][c], fpan);
        }
      }
  }

  free(utf32text);
}

int main(int argc, char* argv[])
{
  int x = 0, y = 0, maxx = -1, maxy = -1, c;
  char* pext;

  if (argc < 2 ||
      argc > 2 ||
      !(pext = strrchr(argv[1], '.')) ||
      (strcmp(pext, ".txt") && strcmp(pext, ".TXT")))
  {
    printf("Sample usage:\n  mkfnt 512_8_bold.txt\n"
           "This will create \"font[.fnt,.tab,.pgm,.h,...]\" files from "
           "character descriptions found in \"512_8_bold.txt\".\n");
    return EXIT_FAILURE;
  }

  if (!(fin = fopen(argv[1], "r")))
  {
lopen:
    printf("Can't open or create file \"%s\"!\n", argv[1]);
    return EXIT_FAILURE;
  }
  strcpy(pext, ".fnt"); if (!(    ffnt = fopen(argv[1], "wb"))) goto lopen;
  strcpy(pext, ".mi");  if (!(     fmi = fopen(argv[1], "w"))) goto lopen;
  strcpy(pext, ".vh");  if (!(     fvh = fopen(argv[1], "w"))) goto lopen;
  strcpy(pext, ".h");   if (!(      fh = fopen(argv[1], "w"))) goto lopen;
  strcpy(pext, ".pgm"); if (!(    fimg = fopen(argv[1], "wb"))) goto lopen;
  strcpy(pext, ".tab"); if (!(    ftab = fopen(argv[1], "w"))) goto lopen;
  strcpy(pext, ".hsh"); if (!(fhashtab = fopen(argv[1], "w"))) goto lopen;
  strcpy(pext, ".bf");  if (!(    fbfm = fopen(argv[1], "w"))) goto lopen;
  strcpy(pext, ".bf2"); if (!(   fbfm2 = fopen(argv[1], "w"))) goto lopen;
  strcpy(pext, ".ppm"); if (!(    fpan = fopen(argv[1], "wb"))) goto lopen;

  // load lines from the input file into an image/grid of char cells
  c = fgetc(fin);
  while (c != EOF && y < MAXH)
  {
    maxy = y;
    if (c == '\n')
    {
      y++;
      x = 0;
      c = fgetc(fin);
      continue;
    }

    if (x < MAXW)
    {
      maxx = (maxx < x) ? x : maxx;
      img[y][x++] = c;
    }

    c = fgetc(fin);
  }

  printf("maxx:%d/%d, maxy:%d/%d\n", maxx, MAXW-1, maxy, MAXH-1);

  for (y = 0; y <= maxy; y++)
    for (x = 0; x <= maxx; x++)
      if (bit(img[y][x]) >= 0)
        process(x, y);

  mkimg();
  mktab();
  mkhashtab();
  mkfnt();
  mkbfm();
  mkpan();

  fclose(fpan);
  fclose(fbfm2);
  fclose(fbfm);
  fclose(fhashtab);
  fclose(ftab);
  fclose(fimg);
  fclose(fh);
  fclose(fvh);
  fclose(fmi);
  fclose(ffnt);
  fclose(fin);
  return 0;
}
