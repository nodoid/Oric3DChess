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

void gfx_clear(void)
{
    unsigned char *p = SCR;
    int i;

    for (i = 8000; i; --i)
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
}

void gfx_px(int x, int y)
{
    unsigned char *p;

    if ((unsigned int)x >= SW || (unsigned int)y >= SH)
        return;
    p = SCR + y * 40 + div6[x];
    *p = (unsigned char)((*p | 0x40) | bit6[x]);
}

void gfx_clrp(int x, int y)
{
    unsigned char *p;

    if ((unsigned int)x >= SW || (unsigned int)y >= SH)
        return;
    p = SCR + y * 40 + div6[x];
    *p = (unsigned char)(*p & (unsigned char)~bit6[x]);
}

void gfx_span(int x0, int x1, int y, unsigned char mode)
{
    int c, c0, c1;

    if (y < 0 || y >= SH)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 > SW - 1)
        x1 = SW - 1;
    if (x0 > x1)
        return;

    if (mode == FILL_D50 || mode == FILL_D25) {
        for (c = x0; c <= x1; ++c) {
            unsigned char on;
            if (mode == FILL_D50)
                on = (unsigned char)(((c ^ y) & 1) != 0);
            else
                on = (unsigned char)((c & 1) == 0 && (y & 1) == 0);
            if (on)
                gfx_px(c, y);
            else
                gfx_clrp(c, y);
        }
        return;
    }
    if (mode == FILL_NONE || mode == FILL_D50)
        return;

    c0 = x0 / 6;
    c1 = x1 / 6;
    {
        unsigned char *p = SCR + y * 40 + c0;
        for (c = c0; c <= c1; ++c, ++p) {
            int lo = c * 6;
            int hi = c * 6 + 5;
            unsigned char m;

            if (lo < x0)
                lo = x0;
            if (hi > x1)
                hi = x1;
            m = (unsigned char)((0xFF << (5 - (hi - c * 6))) &
                                (0xFF >> (2 + (lo - c * 6))));
            if (mode == FILL_W)
                *p = (unsigned char)((*p | 0x40) | m);
            else
                *p = (unsigned char)(*p & (unsigned char)~m);
        }
    }
}

void gfx_line(int x0, int y0, int x1, int y1, unsigned char color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0; /* negative */
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        int e2;

        if (color)
            gfx_px(x0, y0);
        else
            gfx_clrp(x0, y0);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = err << 1;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
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
    long x;    /* 16.16 */
    long dx;   /* 16.16 per scanline */
    int ys, ye;
} Edge;

void poly_fill(const int *xs, const int *ys, unsigned char n,
              unsigned char mode)
{
    Edge edges[8];
    unsigned char ne = 0;
    unsigned char i;
    int miny = 32767, maxy = -32767;
    int y;
    int cross[8];

    if (mode == FILL_NONE)
        return;

    for (i = 0; i < n; ++i) {
        unsigned char j = (unsigned char)((i + 1) % n);
        int ax = xs[i], ay = ys[i], bx = xs[j], by = ys[j];

        if (ay == by)
            continue;
        if (ay < by) {
            edges[ne].ys = ay;
            edges[ne].ye = by;
            edges[ne].x = (long)ax << 16;
            edges[ne].dx = ((long)(bx - ax) << 16) / (by - ay);
        } else {
            edges[ne].ys = by;
            edges[ne].ye = ay;
            edges[ne].x = (long)bx << 16;
            edges[ne].dx = ((long)(ax - bx) << 16) / (ay - by);
        }
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

        for (i = 0; i < ne; ++i) {
            if (edges[i].ys <= y && y < edges[i].ye) {
                cross[nc++] = (int)(edges[i].x >> 16);
                edges[i].x += edges[i].dx;
            }
        }
        /* insertion sort, tiny n */
        for (a = 1; a < nc; ++a) {
            int key = cross[a];
            b = (unsigned char)a;
            while (b > 0 && cross[b - 1] > key) {
                cross[b] = cross[b - 1];
                --b;
            }
            cross[b] = key;
        }
        for (i = 0; i + 1 < nc; i = (unsigned char)(i + 2))
            gfx_span(cross[i], cross[i + 1], y, mode);
    }
}

/* ------------------------------------------------------------------ */
/* 3D scene                                                            */
/* ------------------------------------------------------------------ */
int grid_x[81];
int grid_y[81];
int grid_z[81];
int vx[72], vy[72], vz[72]; /* projected piece verts */

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

    /* order faces far -> near (painter's algorithm); larger z = farther */
    for (i = 0; i < nf; ++i)
        order[i] = i;
    for (i = 1; i < nf; ++i) {
        unsigned char key = order[i];
        int kz = face_depth(m, key);
        int s = (int)i - 1;

        while (s >= 0 && face_depth(m, order[s]) < kz) {
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
            unsigned char k2 = (unsigned char)((k + 1) % fc->n);
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
