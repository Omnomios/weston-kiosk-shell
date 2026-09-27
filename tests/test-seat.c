/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <libweston/libweston.h>
#include <libweston/zalloc.h>

/* Exported by libweston 14, but declared only in its internal header.
 * This module is exclusively for headless tests and is never installed. */
void weston_seat_init(struct weston_seat *, struct weston_compositor *, const char *);
void weston_seat_release(struct weston_seat *);
int weston_seat_init_pointer(struct weston_seat *);
int weston_seat_init_keyboard(struct weston_seat *, struct xkb_keymap *);

struct test_seat {
    struct weston_seat seat;
    struct wl_listener destroy;
};

static void destroy_seat(struct wl_listener *listener, void *data)
{
    struct test_seat *test = wl_container_of(listener, test, destroy);
    wl_list_remove(&test->destroy.link);
    weston_seat_release(&test->seat);
    free(test);
}

WL_EXPORT int wet_module_init(struct weston_compositor *compositor, int *argc, char *argv[])
{
    struct test_seat *test = zalloc(sizeof *test);
    if (!test)
        return -1;
    weston_seat_init(&test->seat, compositor, "kiosk-test-seat");
    if (weston_seat_init_pointer(&test->seat) < 0 ||
        weston_seat_init_keyboard(&test->seat, NULL) < 0) {
        weston_seat_release(&test->seat);
        free(test);
        return -1;
    }
    test->destroy.notify = destroy_seat;
    wl_signal_add(&compositor->destroy_signal, &test->destroy);
    return 0;
}
