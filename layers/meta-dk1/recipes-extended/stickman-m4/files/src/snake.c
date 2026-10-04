/* A snake that plays itself, as a moving test pattern for the OLED.
 *
 * The playfield is a 31x13 grid of 4 px cells below a one-line score bar.
 * Only the cells that change are redrawn, and only the affected pages are
 * pushed over I2C, which keeps a step at roughly 25 ms instead of the 95 ms a
 * full frame costs at 100 kHz.
 */
#include "game.h"
#include "ssd1306.h"
#include "rpmsg.h"
#include "trace.h"

#define CELL      4
#define GRID_W    31
#define GRID_H    13
#define ORIGIN_X  2                      /* playfield inset inside the border */
#define ORIGIN_Y  10
#define MAX_LEN   64
#define START_LEN 4
#define BASE_MS   90
#define MANUAL_MS 5000                   /* idle pad time before the AI resumes */

static uint8_t occupied[GRID_H][GRID_W];
static uint8_t seen[GRID_H][GRID_W];
static uint16_t queue[GRID_W * GRID_H];

/* Body as a ring buffer; index 0 of the snake is the head. */
static uint8_t body_x[MAX_LEN], body_y[MAX_LEN];
static uint16_t head, len;
static int8_t dir_x, dir_y;
static uint8_t food_x, food_y;
static uint16_t score, best;
static uint8_t dirty;                    /* pages to push on the next flush */
static uint32_t rng;

/* Set from rpmsg by snake_command(). The pad only requests a direction; the
 * step applies it, so a command arriving mid-step cannot tear the state. */
static int8_t want_x, want_y;
static uint32_t last_cmd;
static int manual, paused, restart_req;

static void wait_polling(uint32_t ms);

static uint32_t rand32(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

static uint16_t seg(uint16_t i)
{
    return (uint16_t)((head + MAX_LEN - i) % MAX_LEN);
}

static void mark(int y, int h)
{
    for (int p = y / 8; p <= (y + h - 1) / 8; p++)
        dirty |= (uint8_t)(1u << p);
}

static void block(uint8_t cx, uint8_t cy, int on, int size)
{
    int x = ORIGIN_X + cx * CELL;
    int y = ORIGIN_Y + cy * CELL;

    for (int i = 0; i < size; i++)
        for (int j = 0; j < size; j++)
            oled_pixel((uint16_t)(x + i), (uint16_t)(y + j), on);
    mark(y, size);
}

/* The head is drawn one pixel wider than the body so the direction of travel
 * reads at a glance; erasing therefore has to clear the whole cell. */
static void draw_head(uint8_t cx, uint8_t cy)   { block(cx, cy, 1, 4); }
static void draw_body(uint8_t cx, uint8_t cy)   { block(cx, cy, 1, 3); }
static void erase_cell(uint8_t cx, uint8_t cy)  { block(cx, cy, 0, 4); }

static void draw_food(uint8_t cx, uint8_t cy, int on)
{
    int x = ORIGIN_X + cx * CELL;
    int y = ORIGIN_Y + cy * CELL;

    oled_pixel((uint16_t)(x + 1), (uint16_t)y, on);
    oled_pixel((uint16_t)x, (uint16_t)(y + 1), on);
    oled_pixel((uint16_t)(x + 1), (uint16_t)(y + 1), on);
    oled_pixel((uint16_t)(x + 2), (uint16_t)(y + 1), on);
    oled_pixel((uint16_t)(x + 1), (uint16_t)(y + 2), on);
    mark(y, 3);
}

static const char *mode_name(void)
{
    if (paused)
        return "PAUSE";
    return manual ? "PAD" : "AI";
}

static void draw_score(void)
{
    char line[20];
    uint32_t i = 0;

    sfmt(line, sizeof(line), "SC%2u HI%2u %s", score, best, mode_name());
    while (line[i])
        i++;
    while (i < 16 && i < sizeof(line) - 1)
        line[i++] = ' ';
    line[i] = '\0';

    oled_text(0, 0, line, 0);
    dirty |= 0x01;
}

static void draw_border(void)
{
    for (uint16_t x = 0; x < OLED_W; x++) {
        oled_pixel(x, 8, 1);
        oled_pixel(x, OLED_H - 1, 1);
    }
    for (uint16_t y = 8; y < OLED_H; y++) {
        oled_pixel(0, y, 1);
        oled_pixel(OLED_W - 1, y, 1);
    }
    dirty = 0xFF;
}

static void place_food(void)
{
    /* Random darts first, then a linear sweep once the snake is long enough
     * that random hits start missing. */
    for (int tries = 0; tries < 64; tries++) {
        uint8_t x = (uint8_t)(rand32() % GRID_W);
        uint8_t y = (uint8_t)(rand32() % GRID_H);

        if (!occupied[y][x]) {
            food_x = x;
            food_y = y;
            draw_food(x, y, 1);
            return;
        }
    }
    for (uint8_t y = 0; y < GRID_H; y++) {
        for (uint8_t x = 0; x < GRID_W; x++) {
            if (!occupied[y][x]) {
                food_x = x;
                food_y = y;
                draw_food(x, y, 1);
                return;
            }
        }
    }
}

/* How many cells the snake could still reach from here. Without this it walks
 * into its own coils and dies within a few dozen steps. */
static int reachable(int sx, int sy)
{
    int qh = 0, qt = 0, n = 0;

    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            seen[y][x] = 0;

    seen[sy][sx] = 1;
    queue[qt++] = (uint16_t)(sy * GRID_W + sx);

    while (qh < qt) {
        uint16_t cell = queue[qh++];
        int cx = cell % GRID_W, cy = cell / GRID_W;
        static const int8_t step[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

        n++;
        for (int i = 0; i < 4; i++) {
            int nx = cx + step[i][0], ny = cy + step[i][1];

            if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H)
                continue;
            if (occupied[ny][nx] || seen[ny][nx])
                continue;
            seen[ny][nx] = 1;
            queue[qt++] = (uint16_t)(ny * GRID_W + nx);
        }
    }
    return n;
}

/* Pick the move with the most room left, breaking ties by distance to the
 * food. Returns 0 when every direction is blocked. */
static int choose_dir(void)
{
    static const int8_t cand[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    uint8_t hx = body_x[head], hy = body_y[head];
    int best_free = -1, best_dist = 0;
    int8_t pick_x = 0, pick_y = 0;

    for (int i = 0; i < 4; i++) {
        int8_t cx = cand[i][0], cy = cand[i][1];

        if (len > 1 && cx == -dir_x && cy == -dir_y)
            continue;

        int nx = hx + cx, ny = hy + cy;
        if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H)
            continue;
        if (occupied[ny][nx])
            continue;

        int free_cells = reachable(nx, ny);
        int dist = iabs(nx - food_x) + iabs(ny - food_y);

        if (free_cells > best_free ||
            (free_cells == best_free && dist < best_dist)) {
            best_free = free_cells;
            best_dist = dist;
            pick_x = cx;
            pick_y = cy;
        }
    }
    if (best_free < 0)
        return 0;

    dir_x = pick_x;
    dir_y = pick_y;
    return 1;
}

static void reset_game(void)
{
    oled_fill(0x00);
    draw_border();

    for (int y = 0; y < GRID_H; y++)
        for (int x = 0; x < GRID_W; x++)
            occupied[y][x] = 0;

    len = START_LEN;
    head = 0;
    dir_x = 1;
    dir_y = 0;
    score = 0;

    uint8_t cy = GRID_H / 2;
    for (uint16_t i = 0; i < len; i++) {
        uint8_t cx = (uint8_t)(GRID_W / 2 - i);

        body_x[seg(i)] = cx;
        body_y[seg(i)] = cy;
        occupied[cy][cx] = 1;
        if (i == 0)
            draw_head(cx, cy);
        else
            draw_body(cx, cy);
    }

    place_food();
    draw_score();
}

static void game_over(uint8_t addr7)
{
    if (score > best)
        best = score;
    tprintf("snake: died at score %u, len %u (best %u)\n", score, len, best);

    for (int i = 0; i < 2; i++) {
        oled_invert(addr7, 1);
        wait_polling(120);
        oled_invert(addr7, 0);
        wait_polling(120);
    }

    oled_text(3, 4, "GAME OVER", 1);
    oled_flush_pages(addr7, 1u << 4);
    wait_polling(1200);
}

/* Commands arrive as single letters over rpmsg: udlr steer, a hands control
 * back to the AI, p pauses, n starts a new game, . is a keepalive. */
void game_command(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        int8_t nx = 0, ny = 0;

        switch (data[i]) {
        case 'u': case 'U': ny = -1; break;
        case 'd': case 'D': ny = 1; break;
        case 'l': case 'L': nx = -1; break;
        case 'r': case 'R': nx = 1; break;
        case 'a': case 'A':
            if (manual) {
                manual = 0;
                tprintf("snake: control handed back to the AI\n");
                draw_score();
            }
            continue;
        case 'p': case 'P':
            paused = !paused;
            tprintf("snake: %s\n", paused ? "paused" : "resumed");
            draw_score();
            continue;
        case 'n': case 'N':
            restart_req = 1;
            continue;
        case '.':
            /* Keepalive: a held d-pad emits one event, so the bridge says it
             * is still there rather than letting the idle timeout fire. */
            if (manual)
                last_cmd = HAL_GetTick();
            continue;
        default:
            continue;                    /* newlines and anything else */
        }

        want_x = nx;
        want_y = ny;
        last_cmd = HAL_GetTick();
        if (!manual) {
            manual = 1;
            tprintf("snake: pad took over\n");
            draw_score();
        }
    }
}

/* Returns 0 when the snake dies. */
static int step_game(void)
{
    if (manual && HAL_GetTick() - last_cmd > MANUAL_MS) {
        manual = 0;
        tprintf("snake: pad idle for %u ms, AI resumes\n", (uint32_t)MANUAL_MS);
        draw_score();
    }

    if (manual) {
        /* Reversing into your own neck is the one input a snake ignores. */
        if ((want_x || want_y) &&
            !(len > 1 && want_x == -dir_x && want_y == -dir_y)) {
            dir_x = want_x;
            dir_y = want_y;
        }
        want_x = 0;
        want_y = 0;
    } else if (!choose_dir()) {
        return 0;
    }

    uint8_t hx = body_x[head], hy = body_y[head];
    int tx = hx + dir_x, ty = hy + dir_y;

    if (tx < 0 || tx >= GRID_W || ty < 0 || ty >= GRID_H)
        return 0;

    uint8_t nx = (uint8_t)tx, ny = (uint8_t)ty;
    uint16_t tail = seg((uint16_t)(len - 1));
    int eating = (nx == food_x && ny == food_y);
    /* The tail vacates this step, so moving onto it is legal unless we grow. */
    int onto_tail = (nx == body_x[tail] && ny == body_y[tail]) && !eating;

    if (occupied[ny][nx] && !onto_tail)
        return 0;

    if (!eating || len >= MAX_LEN) {
        occupied[body_y[tail]][body_x[tail]] = 0;
        erase_cell(body_x[tail], body_y[tail]);
        len--;
    }

    draw_body(hx, hy);                   /* the old head becomes body */
    head = (uint16_t)((head + 1) % MAX_LEN);
    body_x[head] = nx;
    body_y[head] = ny;
    occupied[ny][nx] = 1;
    len++;
    draw_head(nx, ny);

    if (eating) {
        score++;
        if (score > best)
            best = score;
        draw_score();
        place_food();
    }
    return 1;
}

/* HAL_Delay would leave the pad unread for a whole step. */
static void wait_polling(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();

    while (HAL_GetTick() - t0 < ms)
        rpmsg_poll();
}

HAL_StatusTypeDef game_run(uint8_t addr7)
{
    uint32_t errors = 0;

    rng = HAL_GetTick() | 1u;
    tprintf("snake: %ux%u grid, self-playing\n", (uint32_t)GRID_W, (uint32_t)GRID_H);

    reset_game();
    if (oled_flush(addr7) != HAL_OK)
        return HAL_ERROR;
    dirty = 0;

    for (;;) {
        if (restart_req) {
            restart_req = 0;
            tprintf("snake: new game requested\n");
            reset_game();
            if (oled_flush(addr7) != HAL_OK)
                return HAL_ERROR;
            dirty = 0;
        }

        if (!paused && !step_game()) {
            game_over(addr7);
            reset_game();
            if (oled_flush(addr7) != HAL_OK)
                return HAL_ERROR;
            dirty = 0;
        }

        if (dirty) {
            if (oled_flush_pages(addr7, dirty) != HAL_OK) {
                if (++errors > 20) {
                    tprintf("snake: display stopped acking\n");
                    return HAL_ERROR;
                }
            } else {
                errors = 0;
            }
            dirty = 0;
        }

        /* Speeds up as it scores, the way the arcade version does. */
        uint32_t pace = score < 45 ? BASE_MS - score : BASE_MS - 45;
        wait_polling(pace);
    }
}
