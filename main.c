/*
 * WEIRD GENESIS - prototipo v0.1
 * RPG dungeon crawler de sobrevivencia para PSP (homebrew, PSPSDK)
 *
 * Nesta versao: andares gerados proceduralmente, personagens 3D chibi,
 * combate por turnos, fome, ferimentos, ciclo dia/noite, fogueira (save).
 * Textos sem acento de proposito: a fonte embutida do PSP nao tem acentos.
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspctrl.h>
#include <psputils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>

PSP_MODULE_INFO("WeirdGenesis", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

extern unsigned char msx[]; /* fonte 8x8 embutida na libpspdebug */

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define PI_F 3.14159265f

static unsigned int __attribute__((aligned(16))) dlist[262144];

/* ------------------------------------------------------------------ */
/* callbacks de saida (botao HOME)                                      */
/* ------------------------------------------------------------------ */
static int exit_callback(int a1, int a2, void *c) {
    (void)a1; (void)a2; (void)c;
    sceKernelExitGame();
    return 0;
}
static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cb = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int th = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}

/* ------------------------------------------------------------------ */
/* aleatorio                                                            */
/* ------------------------------------------------------------------ */
static unsigned grand(unsigned *s) {
    unsigned x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}
static unsigned game_rng = 2463534242u;
static int rnd(int n) { return (int)(grand(&game_rng) % (unsigned)n); }

/* ------------------------------------------------------------------ */
/* matematica 4x4 (coluna-maior, estilo OpenGL)                         */
/* ------------------------------------------------------------------ */
typedef struct { float m[16]; } M4;

static M4 m4_ident(void) { M4 r; memset(&r, 0, sizeof r); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f; return r; }
static M4 m4_mul(M4 a, M4 b) {
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}
static M4 m4_trans(float x, float y, float z) { M4 r = m4_ident(); r.m[12] = x; r.m[13] = y; r.m[14] = z; return r; }
static M4 m4_scale(float x, float y, float z) { M4 r = m4_ident(); r.m[0] = x; r.m[5] = y; r.m[10] = z; return r; }
static M4 m4_roty(float a) {
    M4 r = m4_ident();
    float c = cosf(a), s = sinf(a);
    r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
    return r;
}
static M4 m4_persp(float fovy_deg, float aspect, float n, float f) {
    float fr = 1.0f / tanf(fovy_deg * 0.5f * PI_F / 180.0f);
    M4 r; memset(&r, 0, sizeof r);
    r.m[0] = fr / aspect; r.m[5] = fr;
    r.m[10] = (f + n) / (n - f); r.m[11] = -1.0f;
    r.m[14] = 2.0f * f * n / (n - f);
    return r;
}
static M4 m4_look(float ex, float ey, float ez, float cx, float cy, float cz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float l = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= l; fy /= l; fz /= l;
    float sx = -fz, sy = 0.0f, sz = fx;
    l = sqrtf(sx * sx + sz * sz);
    sx /= l; sz /= l;
    float ux = sy * fz - sz * fy, uy = sz * fx - sx * fz, uz = sx * fy - sy * fx;
    M4 r = m4_ident();
    r.m[0] = sx; r.m[4] = sy; r.m[8] = sz;
    r.m[1] = ux; r.m[5] = uy; r.m[9] = uz;
    r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
    r.m[12] = -(sx * ex + sy * ey + sz * ez);
    r.m[13] = -(ux * ex + uy * ey + uz * ez);
    r.m[14] = (fx * ex + fy * ey + fz * ez);
    return r;
}
static void set_matrix(int type, const M4 *m) {
    static ScePspFMatrix4 sm __attribute__((aligned(16)));
    memcpy(&sm, m->m, sizeof sm);
    sceGuSetMatrix(type, &sm);
}

/* ------------------------------------------------------------------ */
/* cubos coloridos                                                      */
/* ------------------------------------------------------------------ */
#define RGBA(r, g, b, a) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#define RGB(r, g, b) RGBA(r, g, b, 255)

typedef struct { unsigned int c; float x, y, z; } CV;
#define CV_FLAGS (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

enum {
    C_FLOORA, C_FLOORB, C_WALL, C_WALLB, C_SKIN, C_HAIR, C_SHIRT, C_PANTS, C_EYE, C_RED,
    C_WHITE, C_GREEN, C_DGREEN, C_PINK, C_DPINK, C_GRAY, C_DGRAY, C_BROWN, C_YELLOW,
    C_ORANGE, C_FLAME, C_STAIRS, C_STAIRS2, C_GROUND, C_SHADOW, C_CYAN, C_BLOOD, C_TOOTH,
    C_COUNT
};
static const unsigned palette[C_COUNT] = {
    RGB(74, 66, 84),   RGB(62, 55, 72),   RGB(104, 92, 110), RGB(88, 78, 96),
    RGB(255, 214, 180), RGB(70, 40, 30),  RGB(70, 130, 190), RGB(60, 60, 90), RGB(20, 16, 24), RGB(220, 40, 50),
    RGB(255, 255, 255), RGB(110, 200, 90), RGB(60, 130, 60), RGB(230, 130, 150), RGB(150, 60, 90),
    RGB(150, 150, 160), RGB(80, 80, 92), RGB(130, 84, 48), RGB(250, 220, 80),
    RGB(255, 140, 40), RGB(255, 230, 120), RGB(90, 200, 220), RGB(40, 120, 150), RGB(30, 26, 36), RGBA(0, 0, 0, 255), RGB(120, 230, 255), RGB(130, 20, 30), RGB(240, 240, 220)
};
static CV __attribute__((aligned(16))) cubeV[C_COUNT][36];

static unsigned shade(unsigned col, float k) {
    unsigned r = (col & 255), g = (col >> 8) & 255, b = (col >> 16) & 255;
    r = (unsigned)(r * k); g = (unsigned)(g * k); b = (unsigned)(b * k);
    return RGB(r, g, b);
}
static void build_cube(CV *v, unsigned col) {
    static const float P[8][3] = {
        {-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f},
        {-.5f, -.5f, .5f}, {.5f, -.5f, .5f}, {.5f, .5f, .5f}, {-.5f, .5f, .5f}
    };
    static const int F[6][4] = { {4, 5, 6, 7}, {1, 0, 3, 2}, {0, 4, 7, 3}, {5, 1, 2, 6}, {3, 7, 6, 2}, {0, 1, 5, 4} };
    static const float S[6] = { 0.85f, 0.60f, 0.70f, 0.78f, 1.00f, 0.45f };
    int n = 0;
    for (int f = 0; f < 6; f++) {
        unsigned c = shade(col, S[f]);
        static const int T[6] = { 0, 1, 2, 0, 2, 3 };
        for (int t = 0; t < 6; t++) {
            int idx = F[f][T[t]];
            v[n].c = c; v[n].x = P[idx][0]; v[n].y = P[idx][1]; v[n].z = P[idx][2];
            n++;
        }
    }
}

/* desenha um cubo (centro x,y,z; tamanho sx,sy,sz) sob a matriz base */
static void part(const M4 *base, int ci, float x, float y, float z, float sx, float sy, float sz) {
    M4 t = m4_mul(m4_trans(x, y, z), m4_scale(sx, sy, sz));
    M4 m = m4_mul(*base, t);
    set_matrix(GU_MODEL, &m);
    sceGuDrawArray(GU_TRIANGLES, CV_FLAGS, 36, 0, cubeV[ci]);
}

/* ------------------------------------------------------------------ */
/* texto e retangulos 2D (sprites com a fonte do PSP)                   */
/* ------------------------------------------------------------------ */
typedef struct { float u, v; unsigned int c; float x, y, z; } TV;
#define TV_FLAGS (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)
#define FT_W 128
#define FT_H 256
static unsigned int __attribute__((aligned(16))) fonttex[FT_W * FT_H];

static void build_font(void) {
    memset(fonttex, 0, sizeof fonttex);
    for (int c = 0; c < 256; c++) {
        int gx = (c % 16) * 8, gy = (c / 16) * 8;
        for (int row = 0; row < 8; row++) {
            unsigned char bits = msx[c * 8 + row];
            for (int col = 0; col < 8; col++)
                fonttex[(gy + row) * FT_W + gx + col] = (bits & (0x80 >> col)) ? 0xFFFFFFFFu : 0x00000000u;
        }
    }
    /* bloco branco solido para retangulos: y = 128..135 */
    for (int y = 128; y < 136; y++)
        for (int x = 0; x < 8; x++) fonttex[y * FT_W + x] = 0xFFFFFFFFu;
}

static void rect(int x, int y, int w, int h, unsigned col) {
    TV *v = (TV *)sceGuGetMemory(2 * sizeof(TV));
    float u = 4.0f / FT_W, t = 132.0f / FT_H;
    v[0].u = u; v[0].v = t; v[0].c = col; v[0].x = (float)x; v[0].y = (float)y; v[0].z = 0;
    v[1].u = u; v[1].v = t; v[1].c = col; v[1].x = (float)(x + w); v[1].y = (float)(y + h); v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, TV_FLAGS, 2, 0, v);
}
static void text(int x, int y, int sc, unsigned col, const char *s) {
    int n = (int)strlen(s);
    if (n == 0) return;
    TV *v = (TV *)sceGuGetMemory(n * 2 * sizeof(TV));
    int k = 0, cx = x, cy = y;
    for (int i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)s[i];
        if (ch == '\n') { cx = x; cy += 8 * sc + 2; continue; }
        if (ch != ' ') {
            float u0 = (float)((ch % 16) * 8) / FT_W, v0 = (float)((ch / 16) * 8) / FT_H;
            float u1 = u0 + 8.0f / FT_W, v1 = v0 + 8.0f / FT_H;
            v[k].u = u0; v[k].v = v0; v[k].c = col; v[k].x = (float)cx; v[k].y = (float)cy; v[k].z = 0; k++;
            v[k].u = u1; v[k].v = v1; v[k].c = col; v[k].x = (float)(cx + 8 * sc); v[k].y = (float)(cy + 8 * sc); v[k].z = 0; k++;
        }
        cx += 8 * sc;
    }
    if (k) sceGuDrawArray(GU_SPRITES, TV_FLAGS, k, 0, v);
}
static void text_s(int x, int y, int sc, unsigned col, const char *s) {
    text(x + sc, y + sc, sc, RGB(0, 0, 0), s);
    text(x, y, sc, col, s);
}
static void text_c(int y, int sc, unsigned col, const char *s) {
    int w = (int)strlen(s) * 8 * sc;
    text_s((SCR_W - w) / 2, y, sc, col, s);
}
static void bar(int x, int y, int w, int h, float frac, unsigned col) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    rect(x - 1, y - 1, w + 2, h + 2, RGB(0, 0, 0));
    rect(x, y, w, h, RGB(50, 44, 56));
    if (frac > 0) rect(x, y, (int)(w * frac), h, col);
}
static void panel(int x, int y, int w, int h) {
    rect(x, y, w, h, RGBA(10, 8, 16, 205));
    rect(x, y, w, 1, RGB(150, 130, 170));
    rect(x, y + h - 1, w, 1, RGB(150, 130, 170));
    rect(x, y, 1, h, RGB(150, 130, 170));
    rect(x + w - 1, y, 1, h, RGB(150, 130, 170));
}

/* ------------------------------------------------------------------ */
/* dados do jogo                                                        */
/* ------------------------------------------------------------------ */
#define MW 26
#define MH 26
enum { T_WALL, T_FLOOR, T_STAIRS, T_FIRE };
static unsigned char map[MH][MW];
static int dist_map[MH][MW];
static int startx, starty, firex, firey, stairx, stairy;

typedef struct {
    int x, y, ox, oy;
    int hp, maxhp, atk;
    int type, alive, stun, moved;
} Enemy;
#define MAX_EN 12
static Enemy en[MAX_EN];
static int nen;

typedef struct { int x, y, open; } Chest;
#define MAX_CH 6
static Chest ch[MAX_CH];
static int nch;

typedef struct {
    int x, y, ox, oy;
    float mt, yaw, walkph;
    int hp, maxhp, hunger, rations, bandages;
    int level, xp, atk;
    int legInj, armInj;
    int floor, steps, kills;
    unsigned seed;
} Player;
static Player P;
static int deaths = 0;
static int respawns = 0;

static const char *en_name[3] = { "Gosma", "Carnicento", "Rato Cego" };

enum { S_TITLE, S_EXPLORE, S_BATTLE, S_DEAD };
static int state = S_TITLE;
static unsigned frame = 0;

static char toast_t[96];
static int toast_timer = 0;
static void toast(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(toast_t, sizeof toast_t, fmt, ap);
    va_end(ap);
    toast_timer = 210;
}

static char death_reason[64] = "";
static char savepath[256] = "save.dat";

/* ------------------------------------------------------------------ */
/* geracao de andar (drunkard's walk + BFS)                             */
/* ------------------------------------------------------------------ */
static void gen_floor(unsigned seed) {
    unsigned s = seed * 2654435761u ^ 0x9E3779B9u;
    if (!s) s = 1;
    for (int k = 0; k < 4; k++) grand(&s);
    memset(map, T_WALL, sizeof map);
    int x = MW / 2, y = MH / 2, carved = 1, guard = 0;
    map[y][x] = T_FLOOR;
    startx = x; starty = y;
    while (carved < 190 && guard++ < 60000) {
        int d = (int)(grand(&s) & 3);
        int nx = x + (d == 0) - (d == 1), ny = y + (d == 2) - (d == 3);
        if (nx < 2 || nx > MW - 3 || ny < 2 || ny > MH - 3) continue;
        x = nx; y = ny;
        if (map[y][x] == T_WALL) { map[y][x] = T_FLOOR; carved++; }
        if ((grand(&s) % 12) == 0) {
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int px = x + dx, py = y + dy;
                    if (px >= 1 && px <= MW - 2 && py >= 1 && py <= MH - 2 && map[py][px] == T_WALL) {
                        map[py][px] = T_FLOOR; carved++;
                    }
                }
        }
    }
    /* BFS */
    static int qx[MW * MH], qy[MW * MH];
    for (int j = 0; j < MH; j++) for (int i = 0; i < MW; i++) dist_map[j][i] = -1;
    int qh = 0, qt = 0;
    qx[qt] = startx; qy[qt] = starty; qt++;
    dist_map[starty][startx] = 0;
    int maxd = 0;
    while (qh < qt) {
        int cx = qx[qh], cy = qy[qh]; qh++;
        static const int DX[4] = { 0, 0, -1, 1 }, DY[4] = { -1, 1, 0, 0 };
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx < 0 || ny < 0 || nx >= MW || ny >= MH) continue;
            if (map[ny][nx] == T_WALL || dist_map[ny][nx] >= 0) continue;
            dist_map[ny][nx] = dist_map[cy][cx] + 1;
            if (dist_map[ny][nx] > maxd) maxd = dist_map[ny][nx];
            qx[qt] = nx; qy[qt] = ny; qt++;
        }
    }
    /* escada no ponto mais distante, fogueira a meio caminho */
    stairx = startx; stairy = starty; firex = startx; firey = starty;
    int bestf = 100000;
    for (int j = 0; j < MH; j++)
        for (int i = 0; i < MW; i++) {
            if (dist_map[j][i] == maxd && (stairx == startx && stairy == starty)) { stairx = i; stairy = j; }
            int dd = abs(dist_map[j][i] - maxd / 2);
            if (dist_map[j][i] > 2 && dd < bestf) { bestf = dd; firex = i; firey = j; }
        }
    map[stairy][stairx] = T_STAIRS;
    if (firex == stairx && firey == stairy) { firex = startx + 1; firey = starty; }
    map[firey][firex] = T_FIRE;
}

static Enemy *enemy_at(int x, int y) {
    for (int i = 0; i < nen; i++) if (en[i].alive && en[i].x == x && en[i].y == y) return &en[i];
    return NULL;
}
static Chest *chest_at(int x, int y) {
    for (int i = 0; i < nch; i++) if (ch[i].x == x && ch[i].y == y) return &ch[i];
    return NULL;
}

static void populate(unsigned seed, int with_chests) {
    unsigned s = seed * 747796405u + 2891336453u + (unsigned)respawns * 1013904223u;
    if (!s) s = 7;
    nen = 0;
    int want = 3 + P.floor;
    if (want > 9) want = 9;
    for (int tries = 0; tries < 4000 && nen < want; tries++) {
        int x = 1 + (int)(grand(&s) % (MW - 2)), y = 1 + (int)(grand(&s) % (MH - 2));
        if (map[y][x] != T_FLOOR || dist_map[y][x] < 5) continue;
        if (enemy_at(x, y) || chest_at(x, y)) continue;
        int r = (int)(grand(&s) % 100), type;
        if (P.floor == 1) type = (r < 55) ? 0 : 2;
        else type = (r < 35) ? 0 : (r < 65 ? 2 : 1);
        Enemy *e = &en[nen++];
        e->type = type; e->x = e->ox = x; e->y = e->oy = y; e->alive = 1; e->stun = 0; e->moved = 0;
        int hp = (type == 0) ? 14 : (type == 1 ? 24 : 10);
        int atk = (type == 0) ? 3 : (type == 1 ? 5 : 3);
        hp += (P.floor - 1) * 4; atk += (P.floor - 1) / 2;
        e->hp = e->maxhp = hp; e->atk = atk;
    }
    if (with_chests) {
        nch = 0;
        for (int tries = 0; tries < 4000 && nch < 3; tries++) {
            int x = 1 + (int)(grand(&s) % (MW - 2)), y = 1 + (int)(grand(&s) % (MH - 2));
            if (map[y][x] != T_FLOOR || dist_map[y][x] < 2) continue;
            if (enemy_at(x, y) || chest_at(x, y)) continue;
            ch[nch].x = x; ch[nch].y = y; ch[nch].open = 0; nch++;
        }
    }
}

static void start_floor(unsigned seed) {
    P.seed = seed;
    gen_floor(seed);
    populate(seed, 1);
    P.x = P.ox = startx; P.y = P.oy = starty; P.mt = 1.0f;
}

static void new_game(void) {
    memset(&P, 0, sizeof P);
    P.hp = P.maxhp = 24; P.hunger = 80; P.rations = 2; P.bandages = 1;
    P.level = 1; P.atk = 4; P.floor = 1; P.mt = 1.0f; P.yaw = 0;
    respawns = 0;
    start_floor((unsigned)sceKernelGetSystemTimeLow() | 1u);
    toast("Voce acorda num lugar frio e umido...");
    state = S_EXPLORE;
}

/* ------------------------------------------------------------------ */
/* save / load (so na fogueira)                                         */
/* ------------------------------------------------------------------ */
typedef struct { unsigned magic; int ver; Player p; int deaths; } SaveData;
#define SAVE_MAGIC 0x57474E31u

static int save_game(void) {
    SaveData sd; memset(&sd, 0, sizeof sd);
    sd.magic = SAVE_MAGIC; sd.ver = 1; sd.p = P; sd.deaths = deaths;
    FILE *f = fopen(savepath, "wb");
    if (!f) return 0;
    int ok = fwrite(&sd, sizeof sd, 1, f) == 1;
    fclose(f);
    return ok;
}
static int save_exists(void) {
    FILE *f = fopen(savepath, "rb");
    if (!f) return 0;
    SaveData sd; int ok = fread(&sd, sizeof sd, 1, f) == 1 && sd.magic == SAVE_MAGIC;
    fclose(f);
    return ok;
}
static int load_game(void) {
    FILE *f = fopen(savepath, "rb");
    if (!f) return 0;
    SaveData sd;
    int ok = fread(&sd, sizeof sd, 1, f) == 1 && sd.magic == SAVE_MAGIC;
    fclose(f);
    if (!ok) return 0;
    P = sd.p; deaths = sd.deaths;
    gen_floor(P.seed);
    populate(P.seed, 1);
    for (int i = 0; i < nch; i++) ch[i].open = 1; /* baus ja foram saqueados */
    P.x = P.ox = firex; P.y = P.oy = firey; P.mt = 1.0f;
    state = S_EXPLORE;
    toast("Voce volta para a fogueira.");
    return 1;
}

/* ------------------------------------------------------------------ */
/* batalha                                                              */
/* ------------------------------------------------------------------ */
enum { EV_NONE, EV_PL_HIT_EN, EV_EN_HIT_PL, EV_FOOD, EV_CURE, EV_EN_DIE, EV_INJ_LEG, EV_INJ_ARM, EV_XP, EV_DROP };
typedef struct { char t[80]; int ev, val; } Msg;
#define MQ_MAX 24
static Msg mq[MQ_MAX];
static int mq_n, mq_i;
static Enemy *ben;
static int bphase;       /* 0 = menu, 1 = mensagens */
static int bmenu;
static int bfinal;       /* 0 segue, 1 vitoria, 2 derrota, 3 fuga */
static int bmsg_timer;
static float lunge_pl, lunge_en, flash_pl, flash_en, bdie_scale = 1.0f;
static int inj_queued;

static void bq(int ev, int val, const char *fmt, ...) {
    if (mq_n >= MQ_MAX) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(mq[mq_n].t, sizeof mq[mq_n].t, fmt, ap);
    va_end(ap);
    mq[mq_n].ev = ev; mq[mq_n].val = val;
    mq_n++;
}

static void level_check(void) {
    while (P.xp >= P.level * 18) {
        P.xp -= P.level * 18;
        P.level++; P.maxhp += 5; P.atk++;
        P.hp += 8; if (P.hp > P.maxhp) P.hp = P.maxhp;
        bq(EV_NONE, 0, "Voce subiu para o nivel %d! (+5 HP max, +1 ataque)", P.level);
    }
}

static void apply_event(const Msg *m) {
    switch (m->ev) {
    case EV_PL_HIT_EN: ben->hp -= m->val; if (ben->hp < 0) ben->hp = 0; flash_en = 12; lunge_pl = 1.0f; break;
    case EV_EN_HIT_PL: P.hp -= m->val; if (P.hp < 0) P.hp = 0; flash_pl = 12; lunge_en = 1.0f; break;
    case EV_FOOD: P.hunger += m->val; if (P.hunger > 100) P.hunger = 100; P.rations--; break;
    case EV_CURE: P.hp += m->val; if (P.hp > P.maxhp) P.hp = P.maxhp; P.bandages--; P.legInj = P.armInj = 0; break;
    case EV_EN_DIE: ben->alive = 0; bdie_scale = 0.999f; break;
    case EV_INJ_LEG: P.legInj = 1; break;
    case EV_INJ_ARM: P.armInj = 1; break;
    case EV_XP: P.xp += m->val; P.kills++; level_check(); break;
    case EV_DROP: P.rations += m->val; break;
    default: break;
    }
}

static void msg_show(int i) {
    apply_event(&mq[i]);
    bmsg_timer = 0;
}

static void battle_start_msgs(void) {
    if (mq_n > 0) { bphase = 1; mq_i = 0; msg_show(0); }
    else bphase = 0;
}

static void enemy_turn(int *php) {
    Enemy *e = ben;
    int hits = 1;
    if (e->type == 2 && rnd(100) < 40) hits = 2;
    for (int h = 0; h < hits; h++) {
        if (*php <= 0) break;
        if (rnd(100) < 10) { bq(EV_NONE, 0, "%s erra o ataque!", en_name[e->type]); continue; }
        int dmg = e->atk + rnd(3);
        *php -= dmg;
        bq(EV_EN_HIT_PL, dmg, "%s te acerta e causa %d de dano!", en_name[e->type], dmg);
        if (!inj_queued && *php > 0) {
            if (e->type == 1 && !P.legInj && rnd(100) < 30) {
                bq(EV_INJ_LEG, 0, "Sua perna foi ferida! Fugir ficou dificil.");
                inj_queued = 1;
            } else if (e->type == 0 && !P.armInj && rnd(100) < 20) {
                bq(EV_INJ_ARM, 0, "Acido corroeu seu braco! Seu dano cai.");
                inj_queued = 1;
            }
        }
    }
    if (*php <= 0) { bq(EV_NONE, 0, "Voce caiu..."); bfinal = 2; }
}

static void battle_start(Enemy *e, int ambush) {
    ben = e;
    state = S_BATTLE;
    bmenu = 0; bfinal = 0; mq_n = 0; mq_i = 0; inj_queued = 0;
    lunge_pl = lunge_en = flash_pl = flash_en = 0; bdie_scale = 1.0f;
    bq(EV_NONE, 0, "%s apareceu!", en_name[e->type]);
    if (ambush) {
        int php = P.hp;
        bq(EV_NONE, 0, "Ele ataca de surpresa!");
        enemy_turn(&php);
    }
    battle_start_msgs();
}

static void battle_action(int a) {
    mq_n = 0; mq_i = 0; bfinal = 0; inj_queued = 0;
    int ehp = ben->hp, php = P.hp;
    Enemy *e = ben;
    if (a == 0) {
        if (rnd(100) < 8) {
            bq(EV_NONE, 0, "Voce errou o golpe!");
        } else {
            int dmg = P.atk + rnd(4);
            if (P.armInj) dmg = dmg * 6 / 10;
            if (P.hunger <= 0) dmg = dmg * 6 / 10;
            int crit = rnd(100) < 10;
            if (crit) dmg *= 2;
            if (dmg < 1) dmg = 1;
            ehp -= dmg;
            if (crit) bq(EV_PL_HIT_EN, dmg, "Golpe critico! %d de dano em %s!", dmg, en_name[e->type]);
            else bq(EV_PL_HIT_EN, dmg, "Voce ataca %s e causa %d de dano!", en_name[e->type], dmg);
        }
    } else if (a == 1) {
        if (P.rations <= 0) { bq(EV_NONE, 0, "Voce nao tem racao!"); battle_start_msgs(); return; }
        bq(EV_FOOD, 35, "Voce engole uma racao as pressas. (+35 fome)");
    } else if (a == 2) {
        if (P.bandages <= 0) { bq(EV_NONE, 0, "Voce nao tem bandagem!"); battle_start_msgs(); return; }
        bq(EV_CURE, 12, "Voce se enfaixa. (+12 HP, ferimentos curados)");
        php += 12; if (php > P.maxhp) php = P.maxhp;
    } else {
        int chance = P.legInj ? 25 : 60;
        if (rnd(100) < chance) { bq(EV_NONE, 0, "Voce fugiu!"); bfinal = 3; battle_start_msgs(); return; }
        bq(EV_NONE, 0, "Nao conseguiu fugir!");
    }
    if (ehp <= 0) {
        bq(EV_EN_DIE, 0, "%s morreu.", en_name[e->type]);
        int xp = (e->type == 1 ? 14 : 8) + P.floor * 2;
        bq(EV_XP, xp, "+%d XP", xp);
        if (rnd(100) < 40) bq(EV_DROP, 1, "Ele deixou uma racao.");
        bfinal = 1;
    } else {
        enemy_turn(&php);
    }
    battle_start_msgs();
}

static void battle_after_msgs(void) {
    if (bfinal == 1) { state = S_EXPLORE; }
    else if (bfinal == 2) {
        deaths++;
        snprintf(death_reason, sizeof death_reason, "Morto por %s", en_name[ben->type]);
        state = S_DEAD;
    }
    else if (bfinal == 3) { ben->stun = 3; state = S_EXPLORE; }
    else bphase = 0;
}

/* ------------------------------------------------------------------ */
/* exploracao                                                           */
/* ------------------------------------------------------------------ */
static int is_night(void) { return (P.steps % 150) >= 105; }

static void open_chest(Chest *c) {
    c->open = 1;
    int r = rnd(100);
    if (r < 55) { int n = 1 + rnd(2); P.rations += n; toast("Voce achou %d racao(oes).", n); }
    else if (r < 85) { P.bandages++; toast("Voce achou uma bandagem."); }
    else if (rnd(2)) { P.hp += 10; if (P.hp > P.maxhp) P.hp = P.maxhp; toast("Carne suspeita. Tem gosto de infancia. (+10 HP)"); }
    else { P.hp -= 4; if (P.hp < 1) P.hp = 1; toast("Carne suspeita. Voce passou mal. (-4 HP)"); }
}

static void move_enemies(void) {
    int range = is_night() ? 7 : 4;
    for (int i = 0; i < nen; i++) {
        Enemy *e = &en[i];
        e->ox = e->x; e->oy = e->y; e->moved = 0;
        if (!e->alive) continue;
        if (e->stun > 0) { e->stun--; continue; }
        int dx = P.x - e->x, dy = P.y - e->y;
        if (abs(dx) + abs(dy) > range) continue;
        int tries[2][2]; int n = 0;
        if (abs(dx) >= abs(dy)) {
            tries[n][0] = (dx > 0) - (dx < 0); tries[n][1] = 0; n++;
            tries[n][0] = 0; tries[n][1] = (dy > 0) - (dy < 0); n++;
        } else {
            tries[n][0] = 0; tries[n][1] = (dy > 0) - (dy < 0); n++;
            tries[n][0] = (dx > 0) - (dx < 0); tries[n][1] = 0; n++;
        }
        for (int t = 0; t < n; t++) {
            if (!tries[t][0] && !tries[t][1]) continue;
            int nx = e->x + tries[t][0], ny = e->y + tries[t][1];
            if (map[ny][nx] == T_WALL) continue;
            if (nx == P.x && ny == P.y) continue;
            if (enemy_at(nx, ny)) continue;
            Chest *c = chest_at(nx, ny);
            if (c && !c->open) continue;
            e->x = nx; e->y = ny; e->moved = 1;
            break;
        }
    }
}

static void step_world(void) {
    P.steps++;
    if (P.steps % 5 == 0 && P.hunger > 0) {
        P.hunger--;
        if (P.hunger == 25) toast("Seu estomago ronca. Voce precisa comer.");
        if (P.hunger == 0) toast("Voce esta faminto! Sua vida vai se esvair.");
    }
    if (P.hunger == 0) { P.hp -= 1; }
    if (P.steps % 150 == 105) toast("A noite cai. Os monstros ficam mais atentos.");
    move_enemies();
}

static void try_move(int d) {
    static const int DX[4] = { 0, 0, -1, 1 }, DY[4] = { -1, 1, 0, 0 };
    int nx = P.x + DX[d], ny = P.y + DY[d];
    P.yaw = atan2f((float)DX[d], (float)DY[d]);
    if (map[ny][nx] == T_WALL) return;
    Enemy *e = enemy_at(nx, ny);
    if (e) { battle_start(e, 0); return; }
    Chest *c = chest_at(nx, ny);
    if (c && !c->open) { open_chest(c); return; }
    P.ox = P.x; P.oy = P.y; P.x = nx; P.y = ny; P.mt = 0.0f;
    step_world();
}

static void arrive(void) {
    if (P.hp <= 0) {
        deaths++;
        snprintf(death_reason, sizeof death_reason, "Morto de fome");
        state = S_DEAD;
        return;
    }
    for (int i = 0; i < nen; i++) {
        Enemy *e = &en[i];
        if (!e->alive || e->stun > 0) continue;
        if (abs(e->x - P.x) + abs(e->y - P.y) == 1) {
            battle_start(e, e->moved);
            return;
        }
    }
}

static void use_ration(void) {
    if (P.rations <= 0) { toast("Voce nao tem racao."); return; }
    if (P.hunger >= 95) { toast("Voce nao esta com fome."); return; }
    P.rations--; P.hunger += 35; if (P.hunger > 100) P.hunger = 100;
    toast("Voce comeu uma racao. (+35 fome)");
}
static void use_bandage(void) {
    if (P.bandages <= 0) { toast("Voce nao tem bandagem."); return; }
    if (P.hp >= P.maxhp && !P.legInj && !P.armInj) { toast("Voce nao precisa de bandagem."); return; }
    P.bandages--; P.hp += 12; if (P.hp > P.maxhp) P.hp = P.maxhp;
    P.legInj = P.armInj = 0;
    toast("Voce se enfaixou. (+12 HP)");
}

static void interact(void) {
    int t = map[P.y][P.x];
    if (t == T_STAIRS) {
        P.floor++;
        respawns = 0;
        start_floor(((unsigned)sceKernelGetSystemTimeLow() * 2654435761u) | 1u);
        toast("Voce desce ao andar %d.", P.floor);
    } else if (t == T_FIRE) {
        P.hp += P.maxhp / 2; if (P.hp > P.maxhp) P.hp = P.maxhp;
        P.legInj = P.armInj = 0;
        P.hunger -= 6; if (P.hunger < 1) P.hunger = 1;
        P.steps += 15; /* o tempo passa */
        respawns++;
        populate(P.seed, 0);
        if (save_game()) toast("Descansou e salvou. Os monstros voltaram...");
        else toast("Descansou, mas nao foi possivel salvar.");
    }
}

/* ------------------------------------------------------------------ */
/* desenho 3D                                                           */
/* ------------------------------------------------------------------ */
static float tnow(void) { return (float)frame / 60.0f; }

static void draw_chibi(float x, float y, float z, float yaw, float walk, int flash, int legInj, int armInj) {
    M4 base = m4_mul(m4_trans(x, y, z), m4_roty(yaw));
    float sw = sinf(walk) * 0.12f;
    float bob = fabsf(sinf(walk)) * 0.04f;
    int cs = flash ? C_WHITE : C_SKIN, csh = flash ? C_WHITE : C_SHIRT, cp = flash ? C_WHITE : C_PANTS, chair = flash ? C_WHITE : C_HAIR;
    part(&base, C_SHADOW, 0, 0.02f, 0, 0.55f, 0.02f, 0.45f);
    part(&base, (legInj && !flash) ? C_RED : cp, -0.1f, 0.11f, sw, 0.16f, 0.22f, 0.16f);
    part(&base, cp, 0.1f, 0.11f, -sw, 0.16f, 0.22f, 0.16f);
    part(&base, csh, 0, 0.37f + bob, 0, 0.38f, 0.30f, 0.26f);
    part(&base, (armInj && !flash) ? C_RED : cs, -0.26f, 0.38f + bob, -sw, 0.10f, 0.24f, 0.10f);
    part(&base, cs, 0.26f, 0.38f + bob, sw, 0.10f, 0.24f, 0.10f);
    part(&base, cs, 0, 0.82f + bob, 0, 0.64f, 0.56f, 0.56f);
    part(&base, chair, 0, 1.12f + bob, 0, 0.68f, 0.14f, 0.60f);
    part(&base, chair, 0, 0.88f + bob, -0.27f, 0.68f, 0.50f, 0.10f);
    if (!flash) {
        part(&base, C_EYE, -0.15f, 0.82f + bob, 0.29f, 0.09f, 0.12f, 0.04f);
        part(&base, C_EYE, 0.15f, 0.82f + bob, 0.29f, 0.09f, 0.12f, 0.04f);
    }
}

static void draw_enemy(int type, float x, float y, float z, float yaw, float t, int flash, float scale) {
    M4 base = m4_mul(m4_mul(m4_trans(x, y, z), m4_roty(yaw)), m4_scale(scale, scale, scale));
    float sq = 1.0f + 0.07f * sinf(t * 5.0f + x);
    part(&base, C_SHADOW, 0, 0.02f, 0, 0.7f, 0.02f, 0.6f);
    if (type == 0) { /* Gosma */
        int c = flash ? C_WHITE : C_GREEN;
        part(&base, c, 0, 0.25f * sq, 0, 0.72f, 0.5f * sq, 0.72f);
        part(&base, flash ? C_WHITE : C_DGREEN, 0, 0.58f * sq, 0, 0.5f, 0.14f, 0.5f);
        if (!flash) {
            part(&base, C_EYE, -0.15f, 0.34f * sq, 0.37f, 0.12f, 0.16f, 0.04f);
            part(&base, C_EYE, 0.15f, 0.34f * sq, 0.37f, 0.12f, 0.16f, 0.04f);
            part(&base, C_BLOOD, 0, 0.16f * sq, 0.37f, 0.3f, 0.06f, 0.04f);
        }
    } else if (type == 1) { /* Carnicento */
        int c = flash ? C_WHITE : C_PINK;
        part(&base, c, 0, 0.45f * sq, 0, 0.62f, 0.9f * sq, 0.5f);
        part(&base, flash ? C_WHITE : C_DPINK, 0, 0.95f * sq, 0, 0.5f, 0.14f, 0.4f);
        part(&base, flash ? C_WHITE : C_DPINK, -0.38f, 0.5f, 0, 0.14f, 0.5f, 0.14f);
        part(&base, flash ? C_WHITE : C_DPINK, 0.38f, 0.5f, 0, 0.14f, 0.5f, 0.14f);
        if (!flash) {
            part(&base, C_WHITE, 0, 0.62f * sq, 0.26f, 0.2f, 0.2f, 0.04f);
            part(&base, C_EYE, 0, 0.62f * sq, 0.28f, 0.08f, 0.08f, 0.04f);
            part(&base, C_BLOOD, 0, 0.3f * sq, 0.26f, 0.4f, 0.14f, 0.04f);
            part(&base, C_TOOTH, -0.1f, 0.34f * sq, 0.28f, 0.06f, 0.08f, 0.04f);
            part(&base, C_TOOTH, 0.1f, 0.34f * sq, 0.28f, 0.06f, 0.08f, 0.04f);
        }
    } else { /* Rato Cego */
        int c = flash ? C_WHITE : C_GRAY;
        part(&base, c, 0, 0.2f * sq, 0, 0.45f, 0.34f * sq, 0.8f);
        part(&base, flash ? C_WHITE : C_DGRAY, 0, 0.26f, 0.42f, 0.32f, 0.26f, 0.22f);
        part(&base, flash ? C_WHITE : C_PINK, 0, 0.12f, -0.52f, 0.06f, 0.06f, 0.4f);
        if (!flash) {
            part(&base, C_WHITE, -0.12f, 0.32f, 0.52f, 0.1f, 0.1f, 0.04f);
            part(&base, C_WHITE, 0.12f, 0.32f, 0.52f, 0.1f, 0.1f, 0.04f);
        }
    }
}

static void set_camera(float ex, float ey, float ez, float cx, float cy, float cz) {
    M4 proj = m4_persp(50.0f, (float)SCR_W / SCR_H, 0.5f, 60.0f);
    M4 view = m4_look(ex, ey, ez, cx, cy, cz);
    M4 id = m4_ident();
    set_matrix(GU_PROJECTION, &proj);
    set_matrix(GU_VIEW, &view);
    set_matrix(GU_MODEL, &id);
}

static int wall_visible(int x, int y) {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= MW || ny >= MH) continue;
            if (map[ny][nx] != T_WALL) return 1;
        }
    return 0;
}

static float ease(float t) { return t * t * (3.0f - 2.0f * t); }

static void draw_explore_world(void) {
    float t = tnow();
    float k = ease(P.mt > 1.0f ? 1.0f : P.mt);
    float px = P.ox + (P.x - P.ox) * k, pz = P.oy + (P.y - P.oy) * k;
    set_camera(px, 7.4f, pz + 6.2f, px, 0.0f, pz + 0.3f);
    M4 id = m4_ident();
    int x0 = (int)px - 9, x1 = (int)px + 9, y0 = (int)pz - 7, y1 = (int)pz + 8;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || y < 0 || x >= MW || y >= MH) continue;
            int tl = map[y][x];
            if (tl == T_WALL) {
                if (!wall_visible(x, y)) continue;
                float h = 0.85f + 0.12f * (float)((x * 7 + y * 13) % 3);
                part(&id, ((x + y) & 1) ? C_WALL : C_WALLB, (float)x, h / 2, (float)y, 0.98f, h, 0.98f);
                continue;
            }
            part(&id, ((x + y) & 1) ? C_FLOORA : C_FLOORB, (float)x, -0.1f, (float)y, 0.98f, 0.2f, 0.98f);
            if (tl == T_STAIRS) {
                part(&id, C_STAIRS2, (float)x, 0.02f, (float)y, 0.8f, 0.06f, 0.8f);
                part(&id, C_STAIRS, (float)x, 0.08f, (float)y, 0.5f, 0.06f, 0.5f);
                part(&id, C_CYAN, (float)x, 0.5f + 0.1f * sinf(t * 3), (float)y, 0.14f, 0.5f, 0.14f);
            } else if (tl == T_FIRE) {
                float fl = 0.8f + 0.25f * sinf(t * 11.0f + x);
                part(&id, C_BROWN, (float)x - 0.12f, 0.06f, (float)y, 0.4f, 0.1f, 0.12f);
                part(&id, C_BROWN, (float)x + 0.1f, 0.06f, (float)y + 0.05f, 0.12f, 0.1f, 0.4f);
                part(&id, C_ORANGE, (float)x, 0.22f * fl + 0.1f, (float)y, 0.26f, 0.36f * fl, 0.26f);
                part(&id, C_FLAME, (float)x, 0.3f * fl + 0.12f, (float)y, 0.14f, 0.3f * fl, 0.14f);
            }
        }
    for (int i = 0; i < nch; i++) {
        float cx = (float)ch[i].x, cz = (float)ch[i].y;
        part(&id, C_BROWN, cx, 0.17f, cz, 0.55f, 0.34f, 0.4f);
        part(&id, C_YELLOW, cx, 0.2f, cz + 0.2f, 0.12f, 0.12f, 0.04f);
        if (ch[i].open) part(&id, C_DGRAY, cx, 0.36f, cz - 0.15f, 0.55f, 0.04f, 0.16f);
        else part(&id, C_BROWN, cx, 0.38f, cz, 0.57f, 0.12f, 0.42f);
    }
    for (int i = 0; i < nen; i++) {
        Enemy *e = &en[i];
        if (!e->alive) continue;
        float ex = e->ox + (e->x - e->ox) * k, ez = e->oy + (e->y - e->oy) * k;
        float yaw = atan2f(P.x - ex, P.y - ez);
        draw_enemy(e->type, ex, 0.0f, ez, yaw, t, 0, 1.0f);
    }
    float walk = (P.mt < 1.0f) ? P.mt * 3.14159f * 2.0f : 0.0f;
    draw_chibi(px, 0.0f, pz, P.yaw, walk, 0, P.legInj, P.armInj);
}

static void draw_battle_world(void) {
    float t = tnow();
    set_camera(0.0f, 2.6f, 5.6f, 0.0f, 0.7f, 0.0f);
    M4 id = m4_ident();
    part(&id, C_GROUND, 0, -0.2f, 0, 9.0f, 0.4f, 5.0f);
    for (int i = -4; i <= 4; i++) part(&id, C_WALLB, (float)i, 0.5f, -2.8f, 0.98f, 1.0f, 0.5f);
    float pl_x = -1.7f + lunge_pl * 1.4f, en_x = 1.7f - lunge_en * 1.4f;
    draw_chibi(pl_x, 0, 0, PI_F / 2, 0, flash_pl > 0 && ((int)flash_pl & 2), P.legInj, P.armInj);
    if (bdie_scale > 0.02f || ben->alive)
        draw_enemy(ben->type, en_x, 0, 0, -PI_F / 2, t, flash_en > 0 && ((int)flash_en & 2), bdie_scale);
}

/* ------------------------------------------------------------------ */
/* interface                                                            */
/* ------------------------------------------------------------------ */
static void draw_night_overlay(void) {
    int tod = P.steps % 150, a = 0;
    if (tod >= 80 && tod < 105) a = (tod - 80) * 110 / 25;
    else if (tod >= 105 && tod < 135) a = 110;
    else if (tod >= 135) a = 110 - (tod - 135) * 110 / 15;
    if (a > 0) rect(0, 0, SCR_W, SCR_H, RGBA(10, 10, 50, a));
}

static void draw_explore_ui(void) {
    char b[96];
    panel(4, 4, 150, 78);
    snprintf(b, sizeof b, "HP %d/%d", P.hp, P.maxhp);
    text_s(10, 9, 1, RGB(255, 255, 255), b);
    bar(10, 19, 138, 6, (float)P.hp / P.maxhp, RGB(220, 60, 70));
    snprintf(b, sizeof b, "FOME %d", P.hunger);
    text_s(10, 29, 1, RGB(255, 255, 255), b);
    bar(10, 39, 138, 6, P.hunger / 100.0f, P.hunger <= 25 ? RGB(240, 120, 40) : RGB(230, 190, 70));
    snprintf(b, sizeof b, "Nv %d  XP %d/%d", P.level, P.xp, P.level * 18);
    text_s(10, 50, 1, RGB(200, 230, 255), b);
    snprintf(b, sizeof b, "Racao %d  Band. %d", P.rations, P.bandages);
    text_s(10, 61, 1, RGB(255, 255, 255), b);
    if (P.legInj) text_s(10, 72, 1, RGB(255, 90, 90), "Perna ferida");
    if (P.armInj) text_s(P.legInj ? 100 : 10, 72, 1, RGB(255, 90, 90), "Braco ferido");

    panel(SCR_W - 124, 4, 120, 30);
    snprintf(b, sizeof b, "Andar %d", P.floor);
    text_s(SCR_W - 118, 9, 1, RGB(255, 255, 255), b);
    snprintf(b, sizeof b, "Dia %d - %s", P.steps / 150 + 1, is_night() ? "Noite" : "Dia");
    text_s(SCR_W - 118, 21, 1, is_night() ? RGB(160, 170, 255) : RGB(255, 230, 140), b);

    int t = map[P.y][P.x];
    if (t == T_STAIRS) { panel(120, 236, 240, 18); text_c(241, 1, RGB(120, 230, 255), "X: descer para o proximo andar"); }
    else if (t == T_FIRE) { panel(100, 236, 280, 18); text_c(241, 1, RGB(255, 200, 100), "X: descansar e salvar (monstros voltam)"); }
    else { text_s(6, 262, 1, RGB(190, 190, 200), "Quad: comer   Tri: bandagem"); }
    if (toast_timer > 0) {
        int w = (int)strlen(toast_t) * 8 + 16;
        panel((SCR_W - w) / 2, 210, w, 20);
        text_c(216, 1, RGB(255, 255, 255), toast_t);
    }
}

static const char *menu_names[4] = { "Atacar", "Comer racao", "Bandagem", "Fugir" };

static void draw_battle_ui(void) {
    char b[96];
    panel(4, 4, 200, 34);
    snprintf(b, sizeof b, "Voce  Nv%d  HP %d/%d", P.level, P.hp, P.maxhp);
    text_s(10, 9, 1, RGB(255, 255, 255), b);
    bar(10, 21, 188, 8, (float)P.hp / P.maxhp, RGB(220, 60, 70));
    panel(SCR_W - 204, 4, 200, 34);
    snprintf(b, sizeof b, "%s  HP %d/%d", en_name[ben->type], ben->hp, ben->maxhp);
    text_s(SCR_W - 198, 9, 1, RGB(255, 255, 255), b);
    bar(SCR_W - 198, 21, 188, 8, (float)ben->hp / ben->maxhp, RGB(200, 90, 200));
    panel(4, 40, 110, 22);
    snprintf(b, sizeof b, "FOME %d", P.hunger);
    text_s(10, 46, 1, RGB(255, 255, 255), b);
    if (P.legInj) text_s(10, 66, 1, RGB(255, 90, 90), "Perna ferida");
    if (P.armInj) text_s(10, 77, 1, RGB(255, 90, 90), "Braco ferido");

    panel(4, 196, SCR_W - 8, 72);
    if (bphase == 1) {
        text_s(16, 210, 1, RGB(255, 255, 255), mq[mq_i].t);
        text_s(SCR_W - 60, 254, 1, RGB(180, 180, 200), "[X]");
    } else {
        for (int i = 0; i < 4; i++) {
            int x = 24 + (i % 2) * 190, y = 208 + (i / 2) * 24;
            unsigned col = (i == bmenu) ? RGB(255, 230, 120) : RGB(200, 200, 215);
            char line[48];
            if (i == 1) snprintf(line, sizeof line, "%s (%d)", menu_names[i], P.rations);
            else if (i == 2) snprintf(line, sizeof line, "%s (%d)", menu_names[i], P.bandages);
            else snprintf(line, sizeof line, "%s", menu_names[i]);
            if (i == bmenu) text_s(x - 14, y, 1, RGB(255, 230, 120), ">");
            text_s(x, y, 1, col, line);
        }
    }
}

static int has_save = 0;

static void draw_title_ui(void) {
    text_c(40, 3, RGB(240, 90, 90), "WEIRD GENESIS");
    text_c(76, 1, RGB(200, 190, 220), "prototipo v0.1 - dungeon crawler de sobrevivencia");
    int items = 2;
    for (int i = 0; i < items; i++) {
        const char *n = (i == 0) ? "Novo jogo" : "Continuar";
        unsigned col = (i == 0 || has_save) ? ((i == bmenu) ? RGB(255, 230, 120) : RGB(220, 220, 235)) : RGB(100, 100, 110);
        int y = 176 + i * 20;
        if (i == bmenu) text_s(170, y, 1, RGB(255, 230, 120), ">");
        text_s(186, y, 1, col, n);
    }
    text_c(240, 1, RGB(170, 170, 190), "D-pad/analogico: mover   X: confirmar");
}

static void draw_dead_ui(void) {
    rect(0, 0, SCR_W, SCR_H, RGBA(60, 0, 0, 170));
    text_c(70, 3, RGB(240, 60, 60), "VOCE MORREU");
    text_c(110, 1, RGB(255, 220, 220), death_reason);
    char b[64];
    snprintf(b, sizeof b, "Mortes: %d   Andar: %d   Nivel: %d", deaths, P.floor, P.level);
    text_c(130, 1, RGB(230, 200, 200), b);
    text_c(190, 1, RGB(255, 230, 120), has_save ? "X: voltar para a ultima fogueira" : "X: recomecar do zero");
}

/* ------------------------------------------------------------------ */
/* loop principal                                                       */
/* ------------------------------------------------------------------ */
static void init_gu(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, dlist);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)0x88000, BUF_W);
    sceGuDepthBuffer((void *)0x110000, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void begin_frame(void) {
    sceGuStart(GU_DIRECT, dlist);
    sceGuClearColor(0xFF1C1210);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
}
static void begin_2d(void) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, FT_W, FT_H, FT_W, fonttex);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}
static void end_frame(void) {
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static void init_savepath(int argc, char **argv) {
    if (argc > 0 && argv && argv[0]) {
        strncpy(savepath, argv[0], 200);
        savepath[200] = 0;
        char *p = strrchr(savepath, '/');
        if (p) { p[1] = 0; strcat(savepath, "save.dat"); }
        else strcpy(savepath, "save.dat");
    }
}

static void update_game(const SceCtrlData *pad, unsigned pressed) {
    int up = pressed & PSP_CTRL_UP, down = pressed & PSP_CTRL_DOWN;
    int left = pressed & PSP_CTRL_LEFT, right = pressed & PSP_CTRL_RIGHT;
    int cross = pressed & PSP_CTRL_CROSS;

    if (toast_timer > 0) toast_timer--;
    if (flash_pl > 0) flash_pl -= 1.0f;
    if (flash_en > 0) flash_en -= 1.0f;
    if (lunge_pl > 0) lunge_pl -= 0.05f;
    if (lunge_en > 0) lunge_en -= 0.05f;
    if (lunge_pl < 0) lunge_pl = 0;
    if (lunge_en < 0) lunge_en = 0;
    if (bdie_scale < 1.0f && bdie_scale > 0) bdie_scale -= 0.04f;
    if (bdie_scale < 0) bdie_scale = 0;

    switch (state) {
    case S_TITLE:
        if (up || down) bmenu = 1 - bmenu;
        if (cross) {
            if (bmenu == 0) new_game();
            else if (has_save) load_game();
        }
        break;
    case S_EXPLORE: {
        if (P.mt < 1.0f) {
            P.mt += 0.14f;
            if (P.mt >= 1.0f) { P.mt = 1.0f; arrive(); }
        } else {
            int dir = -1;
            if (pad->Buttons & PSP_CTRL_UP) dir = 0;
            else if (pad->Buttons & PSP_CTRL_DOWN) dir = 1;
            else if (pad->Buttons & PSP_CTRL_LEFT) dir = 2;
            else if (pad->Buttons & PSP_CTRL_RIGHT) dir = 3;
            else {
                int ax = (int)pad->Lx - 128, ay = (int)pad->Ly - 128;
                if (abs(ax) > 60 || abs(ay) > 60) {
                    if (abs(ax) > abs(ay)) dir = ax < 0 ? 2 : 3;
                    else dir = ay < 0 ? 0 : 1;
                }
            }
            if (dir >= 0) try_move(dir);
            if (state == S_EXPLORE) {
                if (cross) interact();
                if (pressed & PSP_CTRL_SQUARE) use_ration();
                if (pressed & PSP_CTRL_TRIANGLE) use_bandage();
            }
        }
        break;
    }
    case S_BATTLE:
        if (bphase == 1) {
            bmsg_timer++;
            if (cross || bmsg_timer > 140) {
                mq_i++;
                if (mq_i < mq_n) msg_show(mq_i);
                else battle_after_msgs();
            }
        } else {
            if (up || down) bmenu = (bmenu + 2) & 3;
            if (left || right) bmenu ^= 1;
            if (cross) battle_action(bmenu);
        }
        break;
    case S_DEAD:
        if (cross) {
            has_save = save_exists();
            if (has_save) load_game();
            else new_game();
            bmenu = 0;
        }
        break;
    }
}

static void render_game(void) {
    begin_frame();
    if (state == S_EXPLORE) {
        draw_explore_world();
    } else if (state == S_BATTLE) {
        draw_battle_world();
    } else if (state == S_TITLE) {
        set_camera(0.0f, 2.2f, 5.0f, 0.0f, 0.8f, 0.0f);
        M4 id = m4_ident();
        part(&id, C_GROUND, 0, -0.2f, 0, 5.0f, 0.4f, 4.0f);
        draw_chibi(0, 0, 0, tnow() * 0.8f, 0, 0, 0, 0);
        draw_enemy(1, -2.2f, 0, -0.4f, 0.6f, tnow(), 0, 0.8f);
        draw_enemy(0, 2.2f, 0, -0.4f, -0.6f, tnow(), 0, 0.8f);
    } else {
        draw_explore_world();
    }
    begin_2d();
    if (state == S_EXPLORE || state == S_DEAD) draw_night_overlay();
    if (state == S_EXPLORE) draw_explore_ui();
    else if (state == S_BATTLE) draw_battle_ui();
    else if (state == S_TITLE) draw_title_ui();
    else draw_dead_ui();
    end_frame();
    frame++;
}

static void game_init(int argc, char **argv) {
    init_savepath(argc, argv);
    build_font();
    for (int i = 0; i < C_COUNT; i++) build_cube(cubeV[i], palette[i]);
    sceKernelDcacheWritebackAll();
    game_rng ^= (unsigned)sceKernelGetSystemTimeLow();
    if (!game_rng) game_rng = 1;
    memset(&P, 0, sizeof P);
    P.maxhp = 24; P.hp = 24; P.mt = 1.0f;
    has_save = save_exists();
    bmenu = 0;
}

#ifndef HOST_TEST
int main(int argc, char **argv) {
    setup_callbacks();
    game_init(argc, argv);
    init_gu();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    SceCtrlData pad;
    unsigned old = 0;
    while (1) {
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned pressed = pad.Buttons & ~old;
        old = pad.Buttons;
        update_game(&pad, pressed);
        render_game();
    }
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
#endif
