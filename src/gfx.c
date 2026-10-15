/*
 * gfx.c - Oric hi-res drawing + the 3D chess renderer.
 *
 * Coordinate systems
 *  - screen: 240x200 pixels, byte at 0xA000 + y*40 + x/6,
 *            pixel bit = 0x20 >> (x%6), bit6 must stay set (data byte).
 *  - world:  1/16 board square units, x right, y up, z toward white.
 *            board corners x,z in [-64..+64]; white plays at -z.
 *  - camera: behind white, above, pitched down ~35 deg (fixed).
 */
#include "gfx.h"
#include "font5x7.h"
#include "models.h"

#define SCR ((unsigned char *)0xA000)

/* ------------------------------------------------------------------ */
/* camera / projection                                                 */
/* ------------------------------------------------------------------ */
#define CAM_Y 72      /* camera height, 4.5 squares            */
#define CAM_Z (-128)  /* 8 squares behind white's back edge    */
#define SIN_T 147     /* sin(35 deg) * 256                     */
#define COS_T 210     /* cos(35 deg) * 256                     */
#define FOC 168       /* focal length in pixels                */
#define CCX 120       /* screen centre x                       */
#define CCY 96        /* screen centre y                       */

int prj_x, prj_y, prj_z;

void project(int x, int y, int z)
{
    int ry = y - CAM_Y;
    int rz = z - CAM_Z;
    int yv256 = ry * COS_T + rz * SIN_T;
    long zv256 = (long)rz * COS_T - (long)ry * SIN_T;
    int zv = (int)(zv256 >> 8);
    int yv;

    if (zv < 16)
        zv = 16;
    yv = yv256 >> 8;
    prj_x = CCX + (FOC * x) / zv;
    prj_y = CCY - (FOC * yv) / zv;
    prj_z = zv;
}

/* ------------------------------------------------------------------ */
/* primitives                                                          */
/* ------------------------------------------------------------------ */
/* x/6 byte index and x%6 pixel bit, precomputed: the cc65 division
 * helpers cost ~600 cycles each and were called once per pixel. */
static unsigned char div6[SW];
static unsigned char bit6[SW];

/* 6*c for column c (avoids a multiply in span edge maths) */
static const unsigned char mul6[40] = {
    0, 6, 12, 18, 24, 30, 36, 42, 48, 54,
    60, 66, 72, 78, 84, 90, 96, 102, 108, 114,
    120, 126, 132, 138, 144, 150, 156, 162, 168, 174,
    180, 186, 192, 198, 204, 210, 216, 222, 228, 234
};

/* Single-pixel ops are macros: as functions each call cost ~150 cycles
 * of cc65 soft-stack argument shuffling, and gfx_line/gfx_text call
 * them once per pixel (the trace showed ~47% of init time inside the
 * cc65 runtime helpers).  Args are always side-effect-free here. */
#define gfx_px(x, y)                                                         \
    do {                                                                     \
        int gx_ = (int)(x), gy_ = (int)(y);                                 \
        if ((unsigned)gx_ < SW && (unsigned)gy_ < SH) {                      \
            unsigned char *gp_ = SCR + gy_ * 40 + div6[gx_];                \
            *gp_ = (unsigned char)((*gp_ | 0x40) | bit6[gx_]);              \
        }                                                                    \
    } while (0)

#define gfx_clrp(x, y)                                                       \
    do {                                                                     \
        int gx_ = (int)(x), gy_ = (int)(y);                                 \
        if ((unsigned)gx_ < SW && (unsigned)gy_ < SH) {                      \
            unsigned char *gp_ = SCR + gy_ * 40 + div6[gx_];                \
            *gp_ = (unsigned char)(*gp_ & (unsigned char)~bit6[gx_]);        \
        }                                                                    \
    } while (0)

void gfx_clear(void)
{
    unsigned char *p = SCR;
    int i;

    /* Clear the whole $A000-$BFFF block, not just the 8000 pixel
       bytes: the ULA keeps displaying from the tail ($BF68+, the
       "32 byte hole") for the last screen lines, and BASIC leaves
       space characters there that would show up as vertical stripes
       below the prompt. */
    for (i = 8192; i; --i)
        *p++ = 0x40;
}

void gfx_init(void)
{
    int x;

    for (x = 0; x < SW; ++x) {
        div6[x] = (unsigned char)(x / 6);
        bit6[x] = (unsigned char)(0x20 >> (x - 6 * div6[x]));
    }
    gfx_clear();

    /* Switch the ULA into 50 Hz hi-res mode.  The Oric has no mode
       register: the display mode is a software attribute in the text
       screen stream.  The ROM's own HIRES command ($F920 in BASIC 1.1B)
       writes attribute 30 to $BFDF, the last byte (column 39) of the
       last text line, which turns every displayed line into hi-res.
       Do the same here so a fresh CLOAD+RUN shows the board instead
       of the default 40x28 text screen.

       The CRTC still scans 28 character rows, and emulators such as
       Oricutron keep displaying rows 25-27 below the 200 hi-res
       lines as *text* rows fed from $BF68-$BFE0 - rendered with the
       hi-res RAM charset at $9800, which is uninitialized garbage.
       Space characters there turn into a band of stripes under the
       HUD (the ROM's own HIRES shows the same in Oricutron).  Fill
       the tail with attribute bytes instead: any byte with bits 6+5
       clear decodes as a serial attribute, so 0x00 = "foreground
       colour 0, data 0" renders a solid black block on every one of
       those rows.  Attribute 30 at $BFDF (end of line 27, read later
       in the beam order) switches the *next* frame back to hi-res,
       so the main display stays hi-res. */
    {
        unsigned char *p;

        for (p = (unsigned char *)0xBF40;
             p < (unsigned char *)0xBFDF; ++p)
            *p = 0x00;              /* serial attribute: black block   */
        *((unsigned char *)0xBFDF) = 30;  /* line 27 end: HIRES next frame*/
    }
}

void gfx_span(int x0, int x1, int y, unsigned char mode)
{
    int c0, c1;
    unsigned char *p;
    unsigned char full, dot = 0;

    if (y < 0 || y >= SH)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 > SW - 1)
        x1 = SW - 1;
    if (x0 > x1)
        return;
    if (mode == FILL_NONE)
        return;

    c0 = div6[x0];
    c1 = div6[x1];
    p = SCR + y * 40 + c0;

    if (mode == FILL_D50 || mode == FILL_D25) {
        /* Dither bytewise: 6*col is even, so pixel i in a byte has the
           same column parity as i; the dot pattern depends only on
           y&1.  new = (old & ~span) | (dot & span): on-dots set,
           off-dots cleared, pixels outside the span untouched. */
        static const unsigned char dith[2] = { 0x15, 0x2A }; /* D50 */
        static const unsigned char dith25[2] = { 0x2A, 0x00 };
        dot = (mode == FILL_D50) ? dith[y & 1] : dith25[y & 1];
        full = (unsigned char)(0x40 | (dot & 0x3F));
    } else if (mode == FILL_W) {
        full = 0x7F;                       /* 6 pixels on, bit6 set   */
    } else {
        full = 0x40;                       /* all pixels off          */
    }

    /* Interior bytes are whole: store a constant.  Only the (at most
       two) edge bytes need per-pixel mask maths - that loop used to
       run for every byte of every scanline and dominated the render. */
    if (c0 == c1) {
        int lo = x0 - mul6[c0], hi = x1 - mul6[c0];
        unsigned char m = (unsigned char)((0xFF << (5 - hi)) &
                                          (0xFF >> (2 + lo)));

        if (mode == FILL_W)
            *p = (unsigned char)((*p | 0x40) | m);
        else if (mode == FILL_B)
            *p = (unsigned char)(*p & (unsigned char)~m);
        else
            *p = (unsigned char)(((*p | 0x40) & (unsigned char)~m) |
                                 (unsigned char)(dot & m));
        return;
    }

    /* first byte: spans never end before it, so hi is unclipped */
    {
        unsigned char m = (unsigned char)(0xFF >> (2 + x0 - mul6[c0]));

        if (mode == FILL_W)
            *p = (unsigned char)((*p | 0x40) | m);
        else if (mode == FILL_B)
            *p = (unsigned char)(*p & (unsigned char)~m);
        else
            *p = (unsigned char)(((*p | 0x40) & (unsigned char)~m) |
                                 (unsigned char)(dot & m));
        ++p;
    }
    for (c0 = c0 + 1; c0 < c1; ++c0)
        *p++ = full;
    /* last byte: lo is unclipped */
    {
        unsigned char m = (unsigned char)((0xFF << (5 - (x1 - mul6[c1]))) &
                                          0x3F);

        if (mode == FILL_W)
            *p = (unsigned char)((*p | 0x40) | m);
        else if (mode == FILL_B)
            *p = (unsigned char)(*p & (unsigned char)~m);
        else
            *p = (unsigned char)(((*p | 0x40) & (unsigned char)~m) |
                                 (unsigned char)(dot & m));
    }
}

void gfx_line(int x0, int y0, int x1, int y1, unsigned char color)
{
    /* Hot-loop variables live in statics, not soft-stack locals: cc65
       costs ~40 cycles per local access ((sp),y) versus ~10 for an
       absolute address, and the trace showed gfx_line at ~18% of the
       whole render.  The row base pointer replaces the per-pixel
       y*40 multiply (a tosmula0 call, ~250 cycles each).  Not
       reentrant - the renderer never is. */
    static int sx0, sy0, sx1, sy1, serr, sdx, sdy, ssx, ssy, rstep;
    static unsigned char *row;
    int e2;

    sx0 = x0; sy0 = y0; sx1 = x1; sy1 = y1;

    sdx = x1 > x0 ? x1 - x0 : x0 - x1;
    sdy = y1 > y0 ? y0 - y1 : y1 - y0; /* negative */
    ssx = x0 < x1 ? 1 : -1;
    ssy = y0 < y1 ? 1 : -1;
    serr = sdx + sdy;
    row = SCR + (int)(sy0 * 40);
    rstep = ssy > 0 ? 40 : -40;

    for (;;) {
        if ((unsigned)sx0 < SW && (unsigned)sy0 < SH) {
            unsigned char *p = row + div6[sx0];
            unsigned char b = bit6[sx0];

            if (color)
                *p = (unsigned char)((*p | 0x40) | b);
            else
                *p = (unsigned char)(*p & (unsigned char)~b);
        }
        if (sx0 == sx1 && sy0 == sy1)
            break;
        e2 = serr << 1;
        if (e2 >= sdy) {
            serr += sdy;
            sx0 += ssx;
        }
        if (e2 <= sdx) {
            serr += sdx;
            sy0 += ssy;
            row += rstep;
        }
    }
}

void gfx_box(int x0, int y0, int x1, int y1, unsigned char mode)
{
    int y;

    for (y = y0; y <= y1; ++y)
        gfx_span(x0, x1, y, mode);
}

void gfx_text(int x, int y, const char *s, unsigned char color)
{
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        unsigned char col, row;

        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 32);
        if (c < 32 || c > 95)
            continue;
        for (col = 0; col < 5; ++col) {
            unsigned char bits = font5x7[(c - 32) * 5 + col];
            for (row = 0; row < 7; ++row) {
                if (bits & (1 << row)) {
                    if (color)
                        gfx_px(x + col, y + row);
                    else
                        gfx_clrp(x + col, y + row);
                }
            }
        }
        x += 6;
    }
}

/* ------------------------------------------------------------------ */
/* convex polygon fill (edge walking DDA)                              */
/* ------------------------------------------------------------------ */
typedef struct {
    int x;     /* crossing at the current scanline          */
    int dx;    /* whole pixels of advance per scanline      */
    int rem;   /* fixed remainder numerator (sign of slope) */
    int acc;   /* accumulated remainder, folds into x       */
    int den;   /* |dy|, fold threshold                      */
    int ys, ye;
} Edge; /* walked by pointer, never indexed (i*14 would call
           the 250-cycle tosmula0 helper for every edge row) */

static Edge pedges[8];
static int pcross[8];

void poly_fill(const int *xs, const int *ys, unsigned char n,
              unsigned char mode)
{
    unsigned char ne = 0;
    unsigned char i;
    int miny = 32767, maxy = -32767;
    int y;

    if (mode == FILL_NONE)
        return;

    for (i = 0; i < n; ++i) {
        unsigned char j = (unsigned char)(i + 1);
        int ax, ay, bx, by, num, dy, q;

        if (j == n)
            j = 0;                     /* wrap: no costly % operator */
        ax = xs[i]; ay = ys[i]; bx = xs[j]; by = ys[j];
        if (ay == by)
            continue;
        if (ay < by) {
            pedges[ne].ys = ay;
            pedges[ne].ye = by;
            pedges[ne].x = ax;
            num = bx - ax;
            dy = by - ay;
        } else {
            pedges[ne].ys = by;
            pedges[ne].ye = ay;
            pedges[ne].x = bx;
            num = ax - bx;
            dy = ay - by;
        }
        /* Exact rational DDA: one 16-bit division replaces the old
           32.16 long division ((num<<16)/dy) that ran per edge. */
        q = num / dy;
        pedges[ne].dx = q;
        pedges[ne].rem = num - q * dy;   /* num % dy, without a mod */
        pedges[ne].acc = 0;
        pedges[ne].den = dy;
        ++ne;
        if (ay < miny)
            miny = ay;
        if (by < miny)
            miny = by;
        if (ay > maxy)
            maxy = ay;
        if (by > maxy)
            maxy = by;
    }
    if (ne < 2)
        return;
    if (miny < 0)
        miny = 0;
    if (maxy > SH - 1)
        maxy = SH - 1;

    for (y = miny; y <= maxy; ++y) {
        unsigned char nc = 0, a, b;
        Edge *e = pedges;
        Edge *eend = pedges + ne;

        for (; e < eend; ++e) {
            if (y < e->ys || y >= e->ye)
                continue;
            pcross[nc++] = e->x;
            e->x += e->dx;
            e->acc += e->rem;
            if (e->acc >= e->den) {
                ++e->x;
                e->acc -= e->den;
            } else if (e->acc <= -e->den) {
                --e->x;
                e->acc += e->den;
            }
        }
        /* insertion sort, tiny n */
        for (a = 1; a < nc; ++a) {
            int key = pcross[a];
            b = (unsigned char)a;
            while (b > 0 && pcross[b - 1] > key) {
                pcross[b] = pcross[b - 1];
                --b;
            }
            pcross[b] = key;
        }
        for (i = 0; i + 1 < nc; i = (unsigned char)(i + 2))
            gfx_span(pcross[i], pcross[i + 1], y, mode);
    }
}

/* ------------------------------------------------------------------ */
/* 3D scene                                                            */
/* ------------------------------------------------------------------ */
int grid_x[81];
int grid_y[81];
int grid_z[81];
int vx[72], vy[72], vz[72]; /* projected piece verts */
static int fdepth[68];       /* per-face depth, precomputed for sorting */

static const signed char face_fill[2][3] = {
    /* white piece */ { FILL_W, FILL_D50, FILL_NONE },
    /* black piece */ { FILL_D50, FILL_D25, FILL_D25 },
};

int face_depth(const Model *m, unsigned char fi)
{
    const Face *fc = &m->f[fi];
    const unsigned char *idx = &fc->i0;
    int z = 0;
    unsigned char k;

    for (k = 0; k < fc->n; ++k)
        z += vz[idx[k]];
    return z / (int)fc->n;
}

void draw_piece(unsigned char f, unsigned char r, unsigned char pc)
{
    const Model *m = &model_table[pc & 7];
    unsigned char black = (unsigned char)(pc & BLACK);
    unsigned char bg_light = (unsigned char)(((f + r) & 1) != 0);
    unsigned char stroke = bg_light ? 0 : 1;
    unsigned char order[68];
    unsigned char i, n = m->nverts;
    unsigned char nf = m->nfaces;
    int bx = f * 16 - 56;
    int bz = r * 16 - 56;

    /* project vertices */
    for (i = 0; i < n; ++i) {
        const Vtx *v = &m->v[i];
        project(bx + v->x, v->y, bz + v->z);
        vx[i] = prj_x;
        vy[i] = prj_y;
        vz[i] = prj_z;
    }

    /* order faces far -> near (painter's algorithm); larger z = farther.
       Depth is computed once per face: face_depth() divides, and the old
       code called it on every insertion-sort comparison. */
    for (i = 0; i < nf; ++i) {
        fdepth[i] = face_depth(m, i);
        order[i] = i;
    }
    for (i = 1; i < nf; ++i) {
        unsigned char key = order[i];
        int kz = fdepth[key];
        int s = (int)i - 1;

        while (s >= 0 && fdepth[order[s]] < kz) {
            order[s + 1] = order[s];
            --s;
        }
        order[s + 1] = key;
    }

    /* draw */
    for (i = 0; i < nf; ++i) {
        const Face *fc = &m->f[order[i]];
        int xs[4], ys[4];
        const unsigned char *idx = &fc->i0;
        unsigned char k;

        for (k = 0; k < fc->n; ++k) {
            xs[k] = vx[idx[k]];
            ys[k] = vy[idx[k]];
        }
        poly_fill(xs, ys, fc->n, face_fill[black != 0][fc->shade]);
        for (k = 0; k < fc->n; ++k) {
            unsigned char k2 = (unsigned char)(k + 1);

            if (k2 == fc->n)
                k2 = 0;               /* wrap: avoids a % per edge */
            gfx_line(xs[k], ys[k], xs[k2], ys[k2], stroke);
        }
    }
}

void scene_draw(const Position *p)
{
    int f, r;

    /* project the 9x9 board grid once */
    for (r = 0; r <= 8; ++r) {
        for (f = 0; f <= 8; ++f) {
            project(f * 16 - 64, 0, r * 16 - 64);
            grid_x[r * 9 + f] = prj_x;
            grid_y[r * 9 + f] = prj_y;
            grid_z[r * 9 + f] = prj_z;
        }
    }

    /* light squares, far ranks first */
    for (r = 7; r >= 0; --r) {
        for (f = 0; f < 8; ++f) {
            int xsq[4], ysq[4];

            if (((f + r) & 1) == 0)
                continue;
            xsq[0] = grid_x[r * 9 + f];
            ysq[0] = grid_y[r * 9 + f];
            xsq[1] = grid_x[r * 9 + f + 1];
            ysq[1] = grid_y[r * 9 + f + 1];
            xsq[2] = grid_x[(r + 1) * 9 + f + 1];
            ysq[2] = grid_y[(r + 1) * 9 + f + 1];
            xsq[3] = grid_x[(r + 1) * 9 + f];
            ysq[3] = grid_y[(r + 1) * 9 + f];
            poly_fill(xsq, ysq, 4, FILL_W);
        }
    }

    /* board outline */
    gfx_line(grid_x[0], grid_y[0], grid_x[8], grid_y[8], 1);
    gfx_line(grid_x[8], grid_y[8], grid_x[80], grid_y[80], 1);
    gfx_line(grid_x[80], grid_y[80], grid_x[72], grid_y[72], 1);
    gfx_line(grid_x[72], grid_y[72], grid_x[0], grid_y[0], 1);

    /* pieces, far rank first */
    for (r = 7; r >= 0; --r)
        for (f = 0; f < 8; ++f) {
            unsigned char pc = p->board[SQ(f, r)];
            if (pc)
                draw_piece((unsigned char)f, (unsigned char)r, pc);
        }
}
