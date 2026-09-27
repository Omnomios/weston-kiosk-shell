/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define TOP ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
#define BOTTOM ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
#define LEFT ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
#define RIGHT ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
#define CHECK(condition) do { if (!(condition)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); } } while (0)

struct context {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_output *output;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_surface *keyboard_focus;
    struct zwlr_layer_shell_v1 *shell;
    struct xdg_wm_base *xdg;
};
struct window {
    struct context *ctx;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    uint32_t serial, width, height, configures;
    bool closed, ack;
};

static void keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) { close(fd); }
static void enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) { ((struct context *)data)->keyboard_focus = surface; }
static void leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) { ((struct context *)data)->keyboard_focus = NULL; }
static void key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {}
static void modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {}
static void repeat(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {}
static const struct wl_keyboard_listener keyboard_listener = { keymap, enter, leave, key, modifiers, repeat };
static void capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    struct context *ctx = data;
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !ctx->keyboard) {
        ctx->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(ctx->keyboard, &keyboard_listener, ctx);
    }
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) {}
static const struct wl_seat_listener seat_listener = { capabilities, seat_name };
static void ping(void *data, struct xdg_wm_base *xdg, uint32_t serial) { xdg_wm_base_pong(xdg, serial); }
static const struct xdg_wm_base_listener xdg_listener = { ping };
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    struct context *ctx = data;
    if (!strcmp(interface, "wl_compositor")) ctx->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, "wl_shm")) ctx->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, "wl_output") && !ctx->output) ctx->output = wl_registry_bind(registry, name, &wl_output_interface, 1);
    else if (!strcmp(interface, "wl_seat") && !ctx->seat) {
        ctx->seat = wl_registry_bind(registry, name, &wl_seat_interface, 5);
        wl_seat_add_listener(ctx->seat, &seat_listener, ctx);
    } else if (!strcmp(interface, "zwlr_layer_shell_v1")) {
        CHECK(version == 4);
        ctx->shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4);
    } else if (!strcmp(interface, "xdg_wm_base")) {
        ctx->xdg = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(ctx->xdg, &xdg_listener, ctx);
    }
}
static void removed(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { global, removed };
static void init(struct context *ctx)
{
    memset(ctx, 0, sizeof *ctx);
    ctx->display = wl_display_connect(NULL);
    CHECK(ctx->display);
    struct wl_registry *registry = wl_display_get_registry(ctx->display);
    wl_registry_add_listener(registry, &registry_listener, ctx);
    CHECK(wl_display_roundtrip(ctx->display) >= 0);
    CHECK(wl_display_roundtrip(ctx->display) >= 0);
    CHECK(ctx->compositor && ctx->shm && ctx->shell && ctx->xdg && ctx->output);
    wl_registry_destroy(registry);
}
static void settle(struct context *ctx)
{
    for (int i = 0; i < 5; ++i) {
        CHECK(wl_display_roundtrip(ctx->display) >= 0);
        usleep(15000);
    }
}
static void layer_configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial, uint32_t w, uint32_t h)
{
    struct window *win = data;
    win->serial = serial; win->width = w; win->height = h; win->configures++;
    if (win->ack) zwlr_layer_surface_v1_ack_configure(layer, serial);
}
static void layer_closed(void *data, struct zwlr_layer_surface_v1 *layer) { ((struct window *)data)->closed = true; }
static const struct zwlr_layer_surface_v1_listener layer_listener = { layer_configure, layer_closed };
static void xdg_configure(void *data, struct xdg_surface *surface, uint32_t serial) { xdg_surface_ack_configure(surface, serial); }
static const struct xdg_surface_listener xdg_surface_listener = { xdg_configure };
static void toplevel_configure(void *data, struct xdg_toplevel *top, int32_t w, int32_t h, struct wl_array *states)
{
    struct window *win = data;
    win->width = w; win->height = h; win->configures++;
}
static void toplevel_close(void *data, struct xdg_toplevel *top) { ((struct window *)data)->closed = true; }
static const struct xdg_toplevel_listener toplevel_listener = { .configure = toplevel_configure, .close = toplevel_close };

static struct wl_buffer *buffer(struct context *ctx, int w, int h, uint32_t colour)
{
    int fd = memfd_create("layer-shell-test", MFD_CLOEXEC);
    CHECK(fd >= 0);
    size_t size = (size_t)w * h * 4;
    CHECK(ftruncate(fd, size) == 0);
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(pixels != MAP_FAILED);
    for (size_t i = 0; i < size / 4; ++i) pixels[i] = colour;
    struct wl_shm_pool *pool = wl_shm_create_pool(ctx->shm, fd, size);
    struct wl_buffer *result = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool); munmap(pixels, size); close(fd);
    return result;
}
static void attach(struct window *win, uint32_t colour)
{
    struct wl_buffer *buf = buffer(win->ctx, win->width, win->height, colour);
    wl_surface_attach(win->surface, buf, 0, 0);
    wl_surface_damage(win->surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(win->surface);
    /* The compositor retains its reference; destroying this proxy is safe. */
    wl_buffer_destroy(buf);
    settle(win->ctx);
}
static void create_layer(struct context *ctx, struct window *win, uint32_t level, uint32_t anchors, int w, int h, int zone)
{
    memset(win, 0, sizeof *win); win->ctx = ctx; win->ack = true;
    win->surface = wl_compositor_create_surface(ctx->compositor);
    win->layer = zwlr_layer_shell_v1_get_layer_surface(ctx->shell, win->surface, ctx->output, level, "protocol-test");
    zwlr_layer_surface_v1_add_listener(win->layer, &layer_listener, win);
    zwlr_layer_surface_v1_set_size(win->layer, w, h);
    zwlr_layer_surface_v1_set_anchor(win->layer, anchors);
    zwlr_layer_surface_v1_set_exclusive_zone(win->layer, zone);
}
static void destroy_layer(struct window *win)
{
    zwlr_layer_surface_v1_destroy(win->layer);
    wl_surface_destroy(win->surface);
    settle(win->ctx);
}
static void check_size(struct window *win, int w, int h)
{
    if (win->width != (uint32_t)w || win->height != (uint32_t)h)
        fprintf(stderr, "size was %ux%u, expected %dx%d\n", win->width, win->height, w, h);
    CHECK(win->width == (uint32_t)w && win->height == (uint32_t)h);
}
static void normal_tests(struct context *ctx, bool hold)
{
    struct window app = { .ctx = ctx }, panel, bottom, overlay, background, left, right;
    app.surface = wl_compositor_create_surface(ctx->compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(ctx->xdg, app.surface);
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.toplevel, &toplevel_listener, &app);
    wl_surface_commit(app.surface); settle(ctx);
    check_size(&app, 800, 600); attach(&app, 0x123456);
    CHECK(ctx->keyboard_focus == app.surface);

    create_layer(ctx, &panel, 2, TOP | LEFT | RIGHT, 0, 52, 52);
    CHECK(panel.configures == 0);
    wl_surface_commit(panel.surface); settle(ctx);
    CHECK(panel.configures == 1); check_size(&panel, 800, 52);
    check_size(&app, 800, 600); // Unmapped panels reserve no space.
    attach(&panel, 0xff0000); check_size(&app, 800, 548);
    CHECK(ctx->keyboard_focus == app.surface); // keyboard=none never steals focus.

    create_layer(ctx, &bottom, 1, BOTTOM | LEFT | RIGHT, 0, 30, 30);
    zwlr_layer_surface_v1_set_margin(bottom.layer, 0, 0, 7, 0);
    wl_surface_commit(bottom.surface); settle(ctx); attach(&bottom, 0x00ff00);
    check_size(&app, 800, 511);
    zwlr_layer_surface_v1_set_exclusive_zone(panel.layer, 82);
    settle(ctx); check_size(&app, 800, 511); // State is double buffered.
    // Weston 14 has no commit-timing protocol. Still check that state queued
    // after a commit does not change the state applied by that commit.
    wl_surface_commit(panel.surface);
    zwlr_layer_surface_v1_set_exclusive_zone(panel.layer, 10); // Pending for a later commit.
    settle(ctx); check_size(&app, 800, 481); // Must use committed zone=82.
    wl_surface_commit(panel.surface); settle(ctx); check_size(&app, 800, 553);
    zwlr_layer_surface_v1_set_exclusive_zone(panel.layer, 82);
    wl_surface_commit(panel.surface); settle(ctx); check_size(&app, 800, 481);
    zwlr_layer_surface_v1_set_margin(panel.layer, 8, 0, 0, 0);
    wl_surface_commit(panel.surface); settle(ctx); check_size(&app, 800, 473);

    create_layer(ctx, &overlay, 3, BOTTOM | RIGHT, 120, 80, 100);
    wl_surface_commit(overlay.surface); settle(ctx); attach(&overlay, 0x0000ff);
    check_size(&app, 800, 473); // Ambiguous corner anchors reserve nothing.
    zwlr_layer_surface_v1_set_keyboard_interactivity(overlay.layer, 1);
    settle(ctx); CHECK(ctx->keyboard_focus == app.surface);
    wl_surface_commit(overlay.surface); settle(ctx);
    CHECK(ctx->keyboard_focus == overlay.surface);
    zwlr_layer_surface_v1_set_keyboard_interactivity(overlay.layer, 0);
    wl_surface_commit(overlay.surface); settle(ctx);
    CHECK(ctx->keyboard_focus == app.surface);
    zwlr_layer_surface_v1_set_layer(overlay.layer, 0);
    wl_surface_commit(overlay.surface); settle(ctx);
    zwlr_layer_surface_v1_set_layer(overlay.layer, 3);
    wl_surface_commit(overlay.surface); settle(ctx);

    create_layer(ctx, &background, 0, TOP | BOTTOM | LEFT | RIGHT, 0, 0, -1);
    wl_surface_commit(background.surface); settle(ctx);
    check_size(&background, 800, 600); attach(&background, 0x444444);
    check_size(&app, 800, 473);

    if (hold) {
        puts("READY: mapped layer surfaces and kiosk application");
        fflush(stdout);
        while (wl_display_dispatch(ctx->display) >= 0) {}
        return;
    }

    wl_surface_attach(panel.surface, NULL, 0, 0);
    wl_surface_commit(panel.surface); settle(ctx); check_size(&app, 800, 563);
    unsigned previous = panel.configures;
    zwlr_layer_surface_v1_set_size(panel.layer, 0, 40);
    zwlr_layer_surface_v1_set_anchor(panel.layer, TOP | LEFT | RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(panel.layer, 40);
    wl_surface_commit(panel.surface); settle(ctx);
    CHECK(panel.configures == previous + 1); check_size(&panel, 800, 40);
    attach(&panel, 0xff0000); check_size(&app, 800, 523);
    destroy_layer(&bottom); check_size(&app, 800, 560);
    destroy_layer(&panel); check_size(&app, 800, 600);
    zwlr_layer_surface_v1_set_keyboard_interactivity(overlay.layer, 1);
    wl_surface_commit(overlay.surface); settle(ctx);
    CHECK(ctx->keyboard_focus == overlay.surface);
    // Destroy wl_surface before its role object: server must detach safely.
    wl_surface_destroy(overlay.surface); settle(ctx);
    CHECK(overlay.closed); CHECK(ctx->keyboard_focus == app.surface);
    zwlr_layer_surface_v1_destroy(overlay.layer); settle(ctx);
    create_layer(ctx, &left, 2, LEFT | TOP | BOTTOM, 30, 0, 30);
    wl_surface_commit(left.surface); settle(ctx); attach(&left, 0xff0000);
    check_size(&app, 770, 600);
    create_layer(ctx, &right, 2, RIGHT | TOP | BOTTOM, 20, 0, 20);
    zwlr_layer_surface_v1_set_margin(right.layer, 0, 7, 0, 0);
    wl_surface_commit(right.surface); settle(ctx); attach(&right, 0x00ff00);
    check_size(&app, 743, 600);
    zwlr_layer_surface_v1_set_exclusive_zone(left.layer, 2000);
    wl_surface_commit(left.surface); settle(ctx); check_size(&app, 1, 600);
    destroy_layer(&left); check_size(&app, 773, 600);
    destroy_layer(&right); check_size(&app, 800, 600);
    destroy_layer(&background);
    xdg_toplevel_destroy(app.toplevel); xdg_surface_destroy(app.xdg_surface);
    wl_surface_destroy(app.surface); settle(ctx);
    puts("PASS: configuration, reservations, margins, double buffering, focus, remap and teardown");
}
static void negative_test(struct context *ctx, const char *test)
{
    struct window win;
    uint32_t expected = 0;
    if (!strcmp(test, "already-committed")) {
        struct wl_surface *surface = wl_compositor_create_surface(ctx->compositor);
        wl_surface_commit(surface); settle(ctx);
        zwlr_layer_shell_v1_get_layer_surface(ctx->shell, surface, NULL, 2, "invalid");
        expected = ZWLR_LAYER_SHELL_V1_ERROR_ALREADY_CONSTRUCTED;
    } else {
        create_layer(ctx, &win, 2, TOP | LEFT | RIGHT, 0, 40, 40);
        if (!strcmp(test, "bad-anchor")) {
            zwlr_layer_surface_v1_set_anchor(win.layer, 16);
            expected = ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_ANCHOR;
        } else if (!strcmp(test, "bad-size")) {
            zwlr_layer_surface_v1_set_anchor(win.layer, TOP);
            wl_surface_commit(win.surface);
            expected = ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SIZE;
        } else if (!strcmp(test, "bad-keyboard")) {
            zwlr_layer_surface_v1_set_keyboard_interactivity(win.layer, 3);
            expected = ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_KEYBOARD_INTERACTIVITY;
        } else if (!strcmp(test, "bad-serial")) {
            zwlr_layer_surface_v1_ack_configure(win.layer, 123456);
            expected = ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE;
        } else if (!strcmp(test, "early-buffer") || !strcmp(test, "unacked-buffer")) {
            if (!strcmp(test, "unacked-buffer")) {
                win.ack = false;
                wl_surface_commit(win.surface); settle(ctx);
            }
            struct wl_buffer *buf = buffer(ctx, 800, 40, 0xff0000);
            wl_surface_attach(win.surface, buf, 0, 0);
            wl_surface_commit(win.surface);
            expected = ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE;
        } else if (!strcmp(test, "role-conflict")) {
            zwlr_layer_shell_v1_get_layer_surface(ctx->shell, win.surface, NULL, 2, "invalid");
            expected = ZWLR_LAYER_SHELL_V1_ERROR_ROLE;
        } else { CHECK(!"unknown test"); }
    }
    for (int i = 0; i < 5 && wl_display_get_error(ctx->display) == 0; ++i)
        wl_display_roundtrip(ctx->display);
    const struct wl_interface *interface;
    uint32_t id;
    uint32_t code = wl_display_get_protocol_error(ctx->display, &interface, &id);
    CHECK(wl_display_get_error(ctx->display) == EPROTO);
    CHECK(code == expected);
    printf("PASS: %s rejected with protocol error %u\n", test, code);
}
int main(int argc, char **argv)
{
    struct context ctx; init(&ctx);
    if (argc > 1 && strcmp(argv[1], "hold")) negative_test(&ctx, argv[1]);
    else normal_tests(&ctx, argc > 1);
    wl_display_disconnect(ctx.display);
    return 0;
}
