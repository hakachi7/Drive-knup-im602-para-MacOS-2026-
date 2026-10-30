/*
 * rastertoim602 - filtro CUPS para Knup KP-IM602 (POS-80C) no macOS
 * Projeto "Knup IM602 para Mac" - Licenca MIT
 *
 * Faz o mesmo que o driver de Windows ("POS Printer Driver V11.3", POS80.GPD):
 *   - o macOS desenha a pagina (PDF, site, etc.) como imagem a 203 dpi
 *   - este filtro converte a imagem em comandos ESC/POS de imagem (GS v 0)
 *   - largura util de 72 mm = 576 pontos
 *   - remove o espaco em branco do fim da pagina, avanca o papel e corta
 *
 * Comandos copiados do POS80.GPD do driver Windows:
 *   inicio        ESC @
 *   gaveta 1      ESC p 0 64 80
 *   bipe          ESC B 4 1
 *   corte         GS V 0   (o que funciona na IM602)
 *   fim           ESC @
 */

#include <cups/cups.h>
#include <cups/raster.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#define MAX_DOTS   576          /* 72 mm a 203 dpi */
#define MAX_BYTES  (MAX_DOTS / 8)
#define BAND_LINES 24           /* linhas por bloco de imagem (altura fixa) */
#define MAX_GAP    80           /* maior espaco em branco mantido: 10 mm */

enum { CUT_PAGE = 0, CUT_END = 1, CUT_NONE = 2 };
enum { MODE_TEXT = 0, MODE_PHOTO = 1 };

typedef struct {
  int cut;          /* quando cortar */
  int feed_mm;      /* avanco antes do corte */
  int mode;         /* texto (limiar) ou foto (pontilhado) */
  int threshold;    /* 0..255: escuridao minima para imprimir um ponto */
  int trim;         /* remover espaco em branco no inicio/fim */
  int drawer;       /* abrir gaveta antes de imprimir */
  int beep;         /* bipar no fim */
} options_t;

static volatile int canceled = 0;
static void on_cancel(int sig) { (void)sig; canceled = 1; }

static void out(const void *p, size_t n) { fwrite(p, 1, n, stdout); }
static void outc(int c) { putchar(c); }

/* ---------- leitura das opcoes (dialogo de impressao + padrao do PPD) ---------- */

/* Le o valor padrao "*DefaultNOME: valor" direto do PPD da fila
   (sem usar a API de PPD da Apple, que esta obsoleta). */
static char ppd_default_buf[64];
static const char *ppd_default(const char *name)
{
  const char *path = getenv("PPD");
  char line[512], key[96];
  FILE *f;
  if (!path || !(f = fopen(path, "r"))) return NULL;
  snprintf(key, sizeof(key), "*Default%s:", name);
  size_t kl = strlen(key);
  const char *res = NULL;
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, key, kl) == 0) {
      char *p = line + kl;
      while (*p == ' ' || *p == '\t') p++;
      size_t n = strcspn(p, " \t\r\n");
      if (n >= sizeof(ppd_default_buf)) n = sizeof(ppd_default_buf) - 1;
      memcpy(ppd_default_buf, p, n); ppd_default_buf[n] = 0;
      res = ppd_default_buf;
      break;
    }
  }
  fclose(f);
  return res;
}

#define ppd_file_t void
static const char *get_opt(const char *name, ppd_file_t *ppd,
                           int num_options, cups_option_t *options)
{
  (void)ppd;
  const char *v = cupsGetOption(name, num_options, options);
  if (v) return v;
  return ppd_default(name);
}

static void read_options(options_t *o, int num_options, cups_option_t *options)
{
  ppd_file_t *ppd = NULL;
  const char *v;

  o->cut = CUT_PAGE; o->feed_mm = 15; o->mode = MODE_TEXT; o->threshold = 110;
  o->trim = 1; o->drawer = 0; o->beep = 0;

  if ((v = get_opt("IMCorte", ppd, num_options, options)) != NULL) {
    if (!strcmp(v, "Fim")) o->cut = CUT_END;
    else if (!strcmp(v, "Nao")) o->cut = CUT_NONE;
    else o->cut = CUT_PAGE;
  }
  if ((v = get_opt("IMAvanco", ppd, num_options, options)) != NULL) {
    int mm = atoi(v);
    if (mm >= 0 && mm <= 60) o->feed_mm = mm;
  }
  if ((v = get_opt("IMModo", ppd, num_options, options)) != NULL)
    o->mode = !strcmp(v, "Foto") ? MODE_PHOTO : MODE_TEXT;
  if ((v = get_opt("IMEscuro", ppd, num_options, options)) != NULL) {
    if (!strcmp(v, "Claro")) o->threshold = 160;
    else if (!strcmp(v, "Escuro")) o->threshold = 70;
    else if (!strcmp(v, "MuitoEscuro")) o->threshold = 40;
    else o->threshold = 110;
  }
  if ((v = get_opt("IMAparar", ppd, num_options, options)) != NULL)
    o->trim = strcmp(v, "Nao") != 0;
  if ((v = get_opt("IMGaveta", ppd, num_options, options)) != NULL)
    o->drawer = !strcmp(v, "Sim");
  if ((v = get_opt("IMBipe", ppd, num_options, options)) != NULL)
    o->beep = !strcmp(v, "Sim");
}

/* ---------- saida ESC/POS ---------- */

/* Tudo vai como imagem (GS v 0) em blocos de altura fixa.
   Espacos em branco tambem vao como linhas vazias da imagem: nesta
   impressora o avanco por ESC J nao bate com a altura da imagem. */
static unsigned char band[BAND_LINES * MAX_BYTES];
static int band_lines = 0;

static void flush_band(void)
{
  if (band_lines == 0) return;
  /* completa o bloco com linhas brancas para manter sempre a mesma altura */
  for (int r = band_lines; r < BAND_LINES; r++) {
    memset(band + (size_t)r * MAX_BYTES, 0, MAX_BYTES);
    band[(size_t)r * MAX_BYTES + MAX_BYTES - 1] = 0x01;
  }
  unsigned char h[8] = { 0x1D, 'v', '0', 0, MAX_BYTES, 0, BAND_LINES, 0 };
  out(h, 8);
  out(band, (size_t)BAND_LINES * MAX_BYTES);
  band_lines = 0;
}

static void emit_row(const unsigned char *row)
{
  unsigned char *dst = band + (size_t)band_lines * MAX_BYTES;
  if (row) memcpy(dst, row, MAX_BYTES);
  else     memset(dst, 0, MAX_BYTES);
  /* A IM602 encolhe sozinha as linhas totalmente brancas ("economia de papel").
     Um ponto minusculo na ultima coluna (borda direita) evita isso
     - foi o metodo do TESTE 4, que saiu com o espacamento certo. */
  int blank = 1;
  for (int i = 0; i < MAX_BYTES; i++) if (dst[i]) { blank = 0; break; }
  if (blank) dst[MAX_BYTES - 1] |= 0x01;
  if (++band_lines == BAND_LINES) flush_band();
}

static void feed_dots(int dots)
{
  while (dots-- > 0) emit_row(NULL);
}

static void cut_paper(void)
{
  flush_band();
  outc(0x1D); outc('V'); outc(0);   /* GS V 0: corte (o mesmo que funcionou nos testes) */
}

/* ---------- conversao de uma linha para 1 bit ---------- */

/* Devolve a "escuridao" 0..255 do ponto x (255 = preto). */
static inline int darkness(const unsigned char *line, int x, const cups_page_header2_t *hd)
{
  int v;
  if (hd->cupsBitsPerPixel == 1) {
    int bit = (line[x >> 3] >> (7 - (x & 7))) & 1;
    v = bit ? 255 : 0;                  /* 1 bit: K (1 = preto) */
    if (hd->cupsColorSpace == CUPS_CSPACE_W || hd->cupsColorSpace == CUPS_CSPACE_SW)
      v = 255 - v;                      /* W: 1 = branco */
    return v;
  }
  if (hd->cupsBitsPerPixel == 8) {
    v = line[x];
  } else if (hd->cupsBitsPerPixel == 24) {   /* RGB, caso o sistema mande colorido */
    const unsigned char *p = line + x * 3;
    v = (p[0] * 30 + p[1] * 59 + p[2] * 11) / 100;
    return 255 - v;
  } else {
    v = line[x * (hd->cupsBitsPerPixel / 8)];
  }
  if (hd->cupsColorSpace == CUPS_CSPACE_K) return v;   /* K: 255 = preto */
  return 255 - v;                                      /* W/SW: 0 = preto */
}

int main(int argc, char *argv[])
{
  int fd = 0, page = 0;
  cups_raster_t *ras;
  cups_page_header2_t hd;
  options_t o;
  int num_options = 0;
  cups_option_t *options = NULL;

  if (argc < 6 || argc > 7) {
    fprintf(stderr, "Uso: rastertoim602 job usuario titulo copias opcoes [arquivo]\n");
    return 1;
  }
  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
    perror("ERROR: nao consegui abrir o arquivo");
    return 1;
  }

  signal(SIGTERM, on_cancel);
  num_options = cupsParseOptions(argv[5], 0, &options);
  read_options(&o, num_options, options);

  fputs("INFO: Knup IM602 driver 1.0.0\n", stderr);
  ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

  /* inicio do trabalho: ESC @ (igual CmdStartDoc do Windows) */
  outc(0x1B); outc('@');
  if (o.drawer) { unsigned char g[5] = { 0x1B, 'p', 0, 0x40, 0x50 }; out(g, 5); }

  unsigned char *line = NULL;
  int *err_cur = NULL, *err_next = NULL;

  while (!canceled && cupsRasterReadHeader2(ras, &hd)) {
    page++;
    fprintf(stderr, "PAGE: %d 1\n", page);
    fprintf(stderr, "DEBUG: pagina %d: %ux%u, %u bpp, espaco %d, %u bytes/linha\n",
            page, hd.cupsWidth, hd.cupsHeight, hd.cupsBitsPerPixel,
            hd.cupsColorSpace, hd.cupsBytesPerLine);

    int width = (int)hd.cupsWidth;
    int dots = width > MAX_DOTS ? MAX_DOTS : width;
    int offset = width > MAX_DOTS ? (width - MAX_DOTS) / 2 : 0;  /* centraliza se sobrar */
    int pad = (MAX_DOTS - dots) / 2;                              /* centraliza se faltar */
    int ydpi = hd.HWResolution[1] ? (int)hd.HWResolution[1] : 203;

    line = realloc(line, hd.cupsBytesPerLine);
    err_cur = realloc(err_cur, sizeof(int) * (MAX_DOTS + 2));
    err_next = realloc(err_next, sizeof(int) * (MAX_DOTS + 2));
    memset(err_cur, 0, sizeof(int) * (MAX_DOTS + 2));
    memset(err_next, 0, sizeof(int) * (MAX_DOTS + 2));

    int pending_blank = 0;     /* linhas brancas ainda nao enviadas */
    int printed_any = 0;

    for (unsigned y = 0; y < hd.cupsHeight && !canceled; y++) {
      if (cupsRasterReadPixels(ras, line, hd.cupsBytesPerLine) < 1) break;

      unsigned char row[MAX_BYTES];
      memset(row, 0, sizeof(row));
      int any = 0;

      for (int x = 0; x < dots; x++) {
        int d = darkness(line, x + offset, &hd);
        int on;
        if (o.mode == MODE_PHOTO) {
          /* Floyd-Steinberg: fotos e logos em tons de cinza */
          int val = d + err_cur[x + 1] / 16;
          on = val >= 128;
          int e = val - (on ? 255 : 0);
          err_cur[x + 2]  += e * 7;
          err_next[x]     += e * 3;
          err_next[x + 1] += e * 5;
          err_next[x + 2] += e * 1;
        } else {
          on = d >= o.threshold;
        }
        if (on) {
          int px = x + pad;
          row[px >> 3] |= (unsigned char)(0x80 >> (px & 7));
          any = 1;
        }
      }
      if (o.mode == MODE_PHOTO) {
        int *t = err_cur; err_cur = err_next; err_next = t;
        memset(err_next, 0, sizeof(int) * (MAX_DOTS + 2));
      }

      if (!any) {
        if (printed_any || !o.trim) pending_blank++;
        continue;
      }
      if (pending_blank) {
        /* espaco grande (ex.: rodape do navegador la no fim da folha) vira no maximo 10 mm */
        if (o.trim && pending_blank > MAX_GAP) pending_blank = MAX_GAP;
        feed_dots(pending_blank);
        pending_blank = 0;
      }
      emit_row(row);
      printed_any = 1;
    }

    /* espaco em branco do fim da pagina: so imprime se "Aparar" estiver desligado */
    if (!o.trim) feed_dots(pending_blank);

    if (o.cut == CUT_PAGE) {
      feed_dots(o.feed_mm * ydpi * 10 / 254);
      cut_paper();
    }
    fflush(stdout);
  }

  if (o.cut == CUT_END && page > 0) {
    feed_dots(o.feed_mm * 203 * 10 / 254);
    cut_paper();
  } else if (o.cut == CUT_NONE && page > 0) {
    feed_dots(o.feed_mm * 203 * 10 / 254);
  }
  flush_band();
  if (o.beep) { unsigned char b[4] = { 0x1B, 'B', 4, 1 }; out(b, 4); }
  outc(0x1B); outc('@');                        /* fim: ESC @ (CmdEndJob) */
  fflush(stdout);

  cupsRasterClose(ras);
  if (fd) close(fd);
  free(line); free(err_cur); free(err_next);
  cupsFreeOptions(num_options, options);

  if (page == 0) { fputs("ERROR: nenhuma pagina recebida\n", stderr); return 1; }
  fputs("INFO: Impresso.\n", stderr);
  return 0;
}
