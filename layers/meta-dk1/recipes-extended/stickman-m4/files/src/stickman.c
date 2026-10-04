/* A stickman who runs, jumps and throws fireballs at things walking in.
 *
 * Positions are kept in eighths of a pixel so the jump arc and the walk
 * speed are not forced to whole-pixel steps at 25 fps. The whole frame is
 * redrawn every tick and oled_flush_dirty() works out which pages actually
 * changed, which is what keeps a redraw down to two or three pages of I2C.
 *
 * Controls (see linux/pad-bridge.py):
 *   l / L   walk left, stop            j   jump
 *   r / R   walk right, stop           f   throw a fireball
 *   p       pause            n   new game            .   keepalive
 */
#include "game.h"
#include "ssd1306.h"
#include "rpmsg.h"
#include "trace.h"

#define SUB        8                     /* position fixed-point: 1 px = 8 */
#define STATUS_H   9                     /* status line plus its rule */
#define GROUND_Y   58
#define FRAME_MS   40                    /* 25 fps target */

#define WALK_V     12                    /* 1.5 px per frame */
#define GRAVITY    3
#define JUMP_V     (-34)
#define FALL_MAX   40
#define STOMP_V    (-24)

#define PLAYER_W   7
#define PLAYER_H   11
#define ENEMY_W    7
#define ENEMY_H    7
#define FIRE_V     32                    /* 4 px per frame */
#define FIRE_COOL  250                   /* ms between throws */

#define MAX_ENEMIES 4
#define MAX_FIRE    3
#define START_LIVES 3
#define INPUT_IDLE  1500                 /* ms before held keys are dropped */
#define HURT_MS     1500

struct platform {
    uint8_t x, y, w;
};

/* Ground plus two ledges. Fixed screen, no scrolling: at this size a static
 * arena reads better than half a scrolling one. */
static const struct platform platforms[] = {
    { 16, 44, 34 },
    { 76, 32, 36 },
};
#define NPLAT (sizeof(platforms) / sizeof(platforms[0]))

static const uint8_t stick_stand[PLAYER_H] = {
    0x1C, /* ..###.. */
    0x22, /* .#...#. */
    0x22, /* .#...#. */
    0x1C, /* ..###.. */
    0x08, /* ...#... */
    0x7F, /* ####### */
    0x08, /* ...#... */
    0x08, /* ...#... */
    0x14, /* ..#.#.. */
    0x22, /* .#...#. */
    0x41, /* #.....# */
};

static const uint8_t stick_walk[PLAYER_H] = {
    0x1C, 0x22, 0x22, 0x1C,
    0x08,
    0x3E, /* .#####. arms swinging */
    0x08,
    0x08,
    0x08, /* ...#... legs together */
    0x18, /* ..##... */
    0x30, /* .##.... */
};

static const uint8_t stick_throw[PLAYER_H] = {
    0x1C, 0x22, 0x22, 0x1C,
    0x08,
    0x0F, /* ...#### arm forward */
    0x08,
    0x08,
    0x14,
    0x22,
    0x41,
};

static const uint8_t walker_a[ENEMY_H] = {
    0x3E, /* .#####. */
    0x6D, /* ##.#.## eyes */
    0x7F,
    0x7F,
    0x3E,
    0x22, /* .#...#. */
    0x63, /* ##...## feet */
};

static const uint8_t walker_b[ENEMY_H] = {
    0x3E, 0x6D, 0x7F, 0x7F, 0x3E,
    0x14, /* ..#.#.. */
    0x36, /* .##.##. */
};

static const uint8_t fire_a[3] = { 0x02, 0x07, 0x02 };   /* .#. ### .#. */
static const uint8_t fire_b[3] = { 0x05, 0x02, 0x05 };   /* #.# .#. #.# */

struct entity {
    int16_t x, y;                        /* eighths of a pixel */
    int16_t vx, vy;
    uint8_t alive;
    uint8_t anim;
};

static struct entity player;
static struct entity enemy[MAX_ENEMIES];
static struct entity fire[MAX_FIRE];
static int8_t facing = 1;
static int on_ground;
static uint16_t score, lives, best;
static uint32_t frame, next_spawn, last_throw, hurt_until;
static uint32_t rng = 1;

/* Frame cost, in milliseconds, readable from Linux through /dev/mem. Nothing
 * on the M4 reads them, so they have to be volatile to survive -O2. */
static volatile uint32_t dbg_draw, dbg_flush, dbg_pages;

/* Input state, written by game_command() and read by the tick. */
static int8_t hold_x;
static int jump_req, fire_req, paused, restart_req;
static uint32_t last_input;

static void wait_polling(uint32_t ms);

static uint32_t rand32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int px(const struct entity *e)
{
    return e->x / SUB;
}

static int py(const struct entity *e)
{
    return e->y / SUB;
}

static int overlap(const struct entity *a, int aw, int ah,
                   const struct entity *b, int bw, int bh)
{
    int ax = px(a), ay = py(a), bx = px(b), by = py(b);

    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

void game_command(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        switch (data[i]) {
        case 'l': hold_x = -1; facing = -1; break;
        case 'r': hold_x = 1;  facing = 1;  break;
        case 'L': if (hold_x < 0) hold_x = 0; break;
        case 'R': if (hold_x > 0) hold_x = 0; break;
        case 'j': case 'J': jump_req = 1; break;
        case 'f': case 'F': fire_req = 1; break;
        case 'p': case 'P':
            paused = !paused;
            tprintf("stick: %s\n", paused ? "paused" : "resumed");
            break;
        case 'n': case 'N':
            restart_req = 1;
            break;
        case '.':
            break;                       /* keepalive only */
        default:
            continue;
        }
        last_input = HAL_GetTick();
    }
}

static void reset_round(void)
{
    player.x = (int16_t)(20 * SUB);
    player.y = (int16_t)((GROUND_Y - PLAYER_H) * SUB);
    player.vx = player.vy = 0;
    player.alive = 1;
    on_ground = 1;
    facing = 1;

    for (int i = 0; i < MAX_ENEMIES; i++)
        enemy[i].alive = 0;
    for (int i = 0; i < MAX_FIRE; i++)
        fire[i].alive = 0;

    next_spawn = HAL_GetTick() + 1500;
}

static void reset_game(void)
{
    score = 0;
    lives = START_LIVES;
    hurt_until = 0;
    reset_round();
}

static void spawn_enemy(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemy[i].alive)
            continue;

        int from_left = rand32() & 1;

        enemy[i].alive = 1;
        enemy[i].x = (int16_t)((from_left ? 0 : OLED_W - ENEMY_W) * SUB);
        enemy[i].y = (int16_t)((GROUND_Y - ENEMY_H) * SUB);
        enemy[i].vx = (int16_t)(from_left ? 5 : -5);
        enemy[i].vy = 0;
        enemy[i].anim = 0;
        return;
    }
}

static void throw_fire(void)
{
    if (HAL_GetTick() - last_throw < FIRE_COOL)
        return;

    for (int i = 0; i < MAX_FIRE; i++) {
        if (fire[i].alive)
            continue;

        fire[i].alive = 1;
        fire[i].x = (int16_t)((px(&player) + (facing > 0 ? PLAYER_W : -3)) * SUB);
        fire[i].y = (int16_t)((py(&player) + 5) * SUB);
        fire[i].vx = (int16_t)(facing * FIRE_V);
        fire[i].vy = 0;
        last_throw = HAL_GetTick();
        return;
    }
}

/* Only lands on a platform when falling onto its top edge, so you can jump up
 * through a ledge the way the Mario games allow. */
static void apply_gravity(void)
{
    int16_t prev_bottom = (int16_t)(player.y + PLAYER_H * SUB);

    player.vy = (int16_t)(player.vy + GRAVITY);
    if (player.vy > FALL_MAX)
        player.vy = FALL_MAX;
    player.y = (int16_t)(player.y + player.vy);

    int16_t bottom = (int16_t)(player.y + PLAYER_H * SUB);
    int left = px(&player), right = left + PLAYER_W;

    on_ground = 0;

    if (player.vy > 0) {
        for (uint32_t i = 0; i < NPLAT; i++) {
            const struct platform *p = &platforms[i];
            int16_t top = (int16_t)(p->y * SUB);

            if (right <= p->x || left >= p->x + p->w)
                continue;
            if (prev_bottom <= top && bottom >= top) {
                player.y = (int16_t)(top - PLAYER_H * SUB);
                player.vy = 0;
                on_ground = 1;
                return;
            }
        }
    }

    if (bottom >= GROUND_Y * SUB) {
        player.y = (int16_t)((GROUND_Y - PLAYER_H) * SUB);
        player.vy = 0;
        on_ground = 1;
    }
    if (player.y < STATUS_H * SUB) {     /* bonked the status line */
        player.y = (int16_t)(STATUS_H * SUB);
        player.vy = 0;
    }
}

static void hurt_player(void)
{
    if (HAL_GetTick() < hurt_until)
        return;                          /* still blinking from the last hit */

    hurt_until = HAL_GetTick() + HURT_MS;
    if (lives)
        lives--;
    player.vy = STOMP_V / 2;
    tprintf("stick: hit, %u lives left\n", (uint32_t)lives);
}

static void tick(void)
{
    uint32_t now = HAL_GetTick();

    if (now - last_input > INPUT_IDLE)
        hold_x = 0;                      /* bridge went away mid-stride */

    player.vx = (int16_t)(hold_x * WALK_V);
    player.x = (int16_t)(player.x + player.vx);
    if (player.x < 0)
        player.x = 0;
    if (player.x > (int16_t)((OLED_W - PLAYER_W) * SUB))
        player.x = (int16_t)((OLED_W - PLAYER_W) * SUB);

    if (jump_req) {
        jump_req = 0;
        if (on_ground)
            player.vy = JUMP_V;
    }
    apply_gravity();

    if (fire_req) {
        fire_req = 0;
        throw_fire();
    }

    for (int i = 0; i < MAX_FIRE; i++) {
        if (!fire[i].alive)
            continue;
        fire[i].x = (int16_t)(fire[i].x + fire[i].vx);
        if (px(&fire[i]) < 0 || px(&fire[i]) > OLED_W - 3)
            fire[i].alive = 0;
    }

    if (now >= next_spawn) {
        spawn_enemy();
        uint32_t gap = 2500u - (score * 100u);
        next_spawn = now + (gap < 900u ? 900u : gap);
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        struct entity *e = &enemy[i];

        if (!e->alive)
            continue;

        e->x = (int16_t)(e->x + e->vx);
        if (px(e) <= 0 || px(e) >= OLED_W - ENEMY_W)
            e->vx = (int16_t)-e->vx;     /* turn at the walls */
        e->anim++;

        for (int f = 0; f < MAX_FIRE; f++) {
            if (fire[f].alive && overlap(&fire[f], 3, 3, e, ENEMY_W, ENEMY_H)) {
                fire[f].alive = 0;
                e->alive = 0;
                score++;
                break;
            }
        }
        if (!e->alive)
            continue;

        if (overlap(&player, PLAYER_W, PLAYER_H, e, ENEMY_W, ENEMY_H)) {
            int feet = py(&player) + PLAYER_H;

            /* Coming down on its head is a kill, anything else is a hit. */
            if (player.vy > 0 && feet <= py(e) + ENEMY_H / 2 + 1) {
                e->alive = 0;
                score++;
                player.vy = STOMP_V;
            } else {
                hurt_player();
            }
        }
    }

    if (score > best)
        best = score;
    frame++;
}

static void draw(void)
{
    char line[20];
    uint32_t now = HAL_GetTick();

    oled_fill(0x00);

    sfmt(line, sizeof(line), "SC%2u", score);
    oled_text(0, 0, line, 0);
    sfmt(line, sizeof(line), "HI%2u", best);
    oled_text(5, 0, line, 0);
    for (uint16_t i = 0; i < lives; i++)
        oled_rect((uint16_t)(OLED_W - 4 - i * 5), 2, 3, 4, 1);
    for (uint16_t x = 0; x < OLED_W; x++)
        oled_pixel(x, STATUS_H - 1, 1);

    for (uint16_t x = 0; x < OLED_W; x++) {
        oled_pixel(x, GROUND_Y, 1);
        if (!(x % 4))
            oled_pixel(x, (uint16_t)(GROUND_Y + 2), 1);   /* ground texture */
    }
    for (uint32_t i = 0; i < NPLAT; i++)
        oled_rect(platforms[i].x, platforms[i].y, platforms[i].w, 2, 1);

    for (int i = 0; i < MAX_ENEMIES; i++)
        if (enemy[i].alive)
            oled_sprite(px(&enemy[i]), py(&enemy[i]),
                        (enemy[i].anim / 4) & 1 ? walker_b : walker_a,
                        ENEMY_W, ENEMY_H, enemy[i].vx > 0);

    for (int i = 0; i < MAX_FIRE; i++)
        if (fire[i].alive)
            oled_sprite(px(&fire[i]), py(&fire[i]),
                        (frame & 1) ? fire_a : fire_b, 3, 3, 0);

    /* Blink while invulnerable, the way Mario flashes after a hit. */
    int blink = now < hurt_until && ((now / 100u) & 1);
    if (!blink) {
        const uint8_t *pose = stick_stand;

        if (now - last_throw < 150u)
            pose = stick_throw;
        else if (hold_x && on_ground)
            pose = (frame / 4) & 1 ? stick_walk : stick_stand;
        oled_sprite(px(&player), py(&player), pose, PLAYER_W, PLAYER_H,
                    facing < 0);
    }

    if (paused)
        oled_text(5, 3, "PAUSE", 1);
}

static void wait_polling(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();

    while (HAL_GetTick() - t0 < ms)
        rpmsg_poll();
}

static void game_over(uint8_t addr7)
{
    char line[20];

    tprintf("stick: game over, score %u (best %u)\n", score, best);

    for (int i = 0; i < 2; i++) {
        oled_invert(addr7, 1);
        wait_polling(120);
        oled_invert(addr7, 0);
        wait_polling(120);
    }

    oled_fill(0x00);
    oled_text(3, 2, "GAME OVER", 1);
    sfmt(line, sizeof(line), "SCORE %u", score);
    oled_text(4, 4, line, 0);
    oled_text(1, 6, "START = RETRY", 0);
    oled_flush(addr7);
    wait_polling(2500);
}

HAL_StatusTypeDef game_run(uint8_t addr7)
{
    uint32_t errors = 0;

    rng = HAL_GetTick() | 1u;
    last_input = HAL_GetTick();
    tprintf("stick: stickman up, %u fps target\n", (uint32_t)(1000 / FRAME_MS));

    reset_game();

    for (;;) {
        uint32_t t0 = HAL_GetTick();

        if (restart_req) {
            restart_req = 0;
            tprintf("stick: new game\n");
            reset_game();
        }

        if (!paused)
            tick();
        draw();

        uint32_t t_drawn = HAL_GetTick();
        dbg_draw = t_drawn - t0;

        if (oled_flush_dirty(addr7) != HAL_OK) {
            if (++errors > 20) {
                tprintf("stick: display stopped acking\n");
                return HAL_ERROR;
            }
        } else {
            errors = 0;
        }
        dbg_flush = HAL_GetTick() - t_drawn;
        dbg_pages = (uint32_t)__builtin_popcount(oled_last_mask());

        if (!lives) {
            game_over(addr7);
            reset_game();
        }

        uint32_t spent = HAL_GetTick() - t0;
        wait_polling(spent < FRAME_MS ? FRAME_MS - spent : 1);
    }
}
