/* SPDX-License-Identifier: MIT
 * Layer-shell integration for the standalone Weston kiosk shell.
 */
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <libweston/shell-utils.h>
#include "layer-shell.h"
#include "shared/helpers.h"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"

#define LAYER_VERSION 4
#define ANCHORS 15u
#define TOP ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
#define BOTTOM ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM
#define LEFT ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
#define RIGHT ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT

struct layer_state {
    uint32_t width, height, anchor, keyboard, layer;
    int32_t zone, top, right, bottom, left;
};

struct layer_configure {
    struct wl_list link;
    uint32_t serial;
};

struct layer_commit {
    struct wl_list link;
    struct layer_state state;
    bool acknowledged;
};

struct layer_surface {
    struct wl_list link;
    struct kiosk_shell *shell;
    struct wl_resource *resource;
    struct weston_surface *surface;
    struct weston_view *view;
    struct weston_output *output;
    struct wl_listener destroy_listener, commit_listener;
    struct layer_state pending, current;
    struct wl_list configures;
    struct wl_list commits;
    uint32_t initial_layer;
    int32_t configured_width, configured_height;
    int32_t surface_width, surface_height;
    bool initialized, acknowledged, mapped, closed;
    bool request_has_buffer;
};

/* Track bare wl_surface commits, which prohibit subsequently assigning this role. */
struct surface_track {
    struct wl_list link;
    struct weston_surface *surface;
    struct wl_listener destroy;
    bool committed;
};

static void track_destroy(struct wl_listener *listener, void *data)
{
    struct surface_track *track = container_of(listener, struct surface_track, destroy);
    wl_list_remove(&track->destroy.link);
    wl_list_remove(&track->link);
    free(track);
}

static void surface_created(struct wl_listener *listener, void *data)
{
    struct kiosk_shell *shell = container_of(listener, struct kiosk_shell, layer_surface_created);
    struct surface_track *track = zalloc(sizeof *track);
    if (!track) {
        struct weston_surface *surface = data;
        if (surface->resource)
            wl_client_post_no_memory(wl_resource_get_client(surface->resource));
        return;
    }
    track->surface = data;
    track->destroy.notify = track_destroy;
    wl_signal_add(&track->surface->destroy_signal, &track->destroy);
    wl_list_insert(&shell->layer_surface_tracks, &track->link);
}

static struct layer_surface *find_layer(struct kiosk_shell *shell, struct weston_surface *surface)
{
    struct layer_surface *layer;
    surface = weston_surface_get_main_surface(surface);
    wl_list_for_each(layer, &shell->layer_surfaces, link)
        if (layer->surface == surface)
            return layer;
    return NULL;
}

static struct layer_surface *exclusive_surface(struct kiosk_shell *shell)
{
    struct layer_surface *layer, *best = NULL;
    wl_list_for_each(layer, &shell->layer_surfaces, link) {
        if (!layer->mapped || layer->closed ||
            layer->current.keyboard != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE ||
            layer->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP)
            continue;
        if (!best || layer->current.layer > best->current.layer)
            best = layer;
    }
    return best;
}

void kiosk_layer_shell_update_focus(struct kiosk_shell *shell)
{
    struct layer_surface *exclusive = exclusive_surface(shell);
    struct kiosk_shell_seat *seat;
    wl_list_for_each(seat, &shell->seat_list, link) {
        struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat->seat);
        struct layer_surface *focused;
        if (!keyboard)
            continue;
        if (exclusive) {
            if (keyboard->focus != exclusive->surface)
                weston_view_activate_input(exclusive->view, seat->seat, WESTON_ACTIVATE_FLAG_NONE);
            continue;
        }
        focused = keyboard->focus ? find_layer(shell, keyboard->focus) : NULL;
        if (!keyboard->focus || (focused && (!focused->mapped || focused->closed ||
            focused->current.keyboard == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE))) {
            struct weston_surface *fallback = seat->focused_surface;
            if (fallback && !weston_surface_is_mapped(fallback))
                fallback = NULL;
            weston_keyboard_set_focus(keyboard, fallback);
        }
    }
}

bool kiosk_layer_shell_activate(struct kiosk_shell *shell, struct weston_view *view,
                               struct weston_seat *seat, uint32_t flags)
{
    struct layer_surface *exclusive = exclusive_surface(shell);
    struct layer_surface *layer = find_layer(shell, view->surface);
    if (exclusive) {
        weston_view_activate_input(exclusive->view, seat, flags);
        return true;
    }
    if (!layer)
        return false;
    if (layer->mapped && layer->current.keyboard != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
        weston_view_activate_input(layer->view, seat, flags);
    return true;
}

static void clear_focus(struct layer_surface *layer)
{
    struct kiosk_shell_seat *seat;
    wl_list_for_each(seat, &layer->shell->seat_list, link) {
        struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat->seat);
        if (keyboard && keyboard->focus &&
            weston_surface_get_main_surface(keyboard->focus) == layer->surface)
            weston_keyboard_set_focus(keyboard, NULL);
    }
}

static void clear_configures(struct layer_surface *layer)
{
    struct layer_configure *config, *tmp;
    wl_list_for_each_safe(config, tmp, &layer->configures, link) {
        wl_list_remove(&config->link);
        free(config);
    }
}

static void configure(struct layer_surface *layer, int32_t width, int32_t height)
{
    struct layer_configure *config;
    if (layer->configured_width == width && layer->configured_height == height)
        return;
    config = zalloc(sizeof *config);
    if (!config) {
        wl_resource_post_no_memory(layer->resource);
        return;
    }
    config->serial = wl_display_next_serial(layer->shell->compositor->wl_display);
    wl_list_insert(layer->configures.prev, &config->link);
    layer->configured_width = width;
    layer->configured_height = height;
    zwlr_layer_surface_v1_send_configure(layer->resource, config->serial, width, height);
}

/* A positive zone is meaningful only for a single edge or that edge plus
 * both perpendicular edges. Ambiguous corner/all-edge anchors reserve nothing. */
static uint32_t exclusive_edge(const struct layer_state *state)
{
    if (state->zone <= 0)
        return 0;
    switch (state->anchor) {
    case TOP: case TOP | LEFT | RIGHT: return TOP;
    case BOTTOM: case BOTTOM | LEFT | RIGHT: return BOTTOM;
    case LEFT: case LEFT | TOP | BOTTOM: return LEFT;
    case RIGHT: case RIGHT | TOP | BOTTOM: return RIGHT;
    default: return 0;
    }
}

static int32_t bounded_size(int64_t size)
{
    return (int32_t)MAX(1, MIN(INT32_MAX, size));
}

static int32_t bounded_position(int64_t position)
{
    return (int32_t)MAX(INT32_MIN, MIN(INT32_MAX, position));
}

static void place(struct layer_surface *layer, const struct weston_geometry *box)
{
    const struct layer_state *s = &layer->current;
    int32_t width = s->width ? (int32_t)s->width :
        bounded_size((int64_t)box->width - s->left - s->right);
    int32_t height = s->height ? (int32_t)s->height :
        bounded_size((int64_t)box->height - s->top - s->bottom);
    int32_t actual_width = layer->mapped ? layer->surface->width : width;
    int32_t actual_height = layer->mapped ? layer->surface->height : height;
    int64_t x = box->x, y = box->y;
    struct weston_coord_global pos = layer->output->pos;

    configure(layer, width, height);
    if ((s->anchor & (LEFT | RIGHT)) == (LEFT | RIGHT))
        x += ((int64_t)box->width - actual_width + s->left - s->right) / 2;
    else if (s->anchor & LEFT)
        x += s->left;
    else if (s->anchor & RIGHT)
        x += (int64_t)box->width - actual_width - s->right;
    else
        x += ((int64_t)box->width - actual_width) / 2;
    if ((s->anchor & (TOP | BOTTOM)) == (TOP | BOTTOM))
        y += ((int64_t)box->height - actual_height + s->top - s->bottom) / 2;
    else if (s->anchor & TOP)
        y += s->top;
    else if (s->anchor & BOTTOM)
        y += (int64_t)box->height - actual_height - s->bottom;
    else
        y += ((int64_t)box->height - actual_height) / 2;
    pos.c.x += bounded_position(x);
    pos.c.y += bounded_position(y);
    weston_view_set_position(layer->view, pos);
    weston_view_set_output(layer->view, layer->output);
    weston_view_set_mask(layer->view, bounded_position(-x), bounded_position(-y),
                         layer->output->width, layer->output->height);
    weston_view_update_transform(layer->view);
    if (layer->mapped)
        weston_view_schedule_repaint(layer->view);
}

static void reserve(struct weston_geometry *box, const struct layer_state *s)
{
    uint32_t edge = exclusive_edge(s);
    int32_t amount;
    if (!edge)
        return;
    int32_t margin = edge == TOP ? s->top : edge == BOTTOM ? s->bottom :
                     edge == LEFT ? s->left : s->right;
    int32_t limit = (edge == TOP || edge == BOTTOM) ? box->height : box->width;
    /* Keep a nonzero application area even if clients request excessive zones. */
    amount = MAX(0, MIN((int64_t)limit - 1, (int64_t)s->zone + margin));
    if (edge == TOP || edge == BOTTOM) {
        box->height -= amount;
        if (edge == TOP) box->y += amount;
    } else {
        box->width -= amount;
        if (edge == LEFT) box->x += amount;
    }
}

void kiosk_layer_shell_arrange(struct kiosk_shell *shell, struct weston_output *output)
{
    struct kiosk_shell_output *shoutput;
    struct layer_surface *layer;
    struct weston_geometry full, usable;
    if (!output || shell->layer_shutting_down)
        return;
    shoutput = kiosk_shell_find_output(shell, output);
    if (!shoutput)
        return;
    full = (struct weston_geometry){ 0, 0, output->width, output->height };
    usable = full;
    /* Reserve mapped exclusive surfaces, highest layer first. Stable ordering
     * within a layer is the order in which its surfaces were created. */
    for (int level = 3; level >= 0; --level) {
        wl_list_for_each_reverse(layer, &shell->layer_surfaces, link) {
            if (layer->output != output || !layer->initialized || layer->closed ||
                layer->current.layer != (uint32_t)level || !exclusive_edge(&layer->current))
                continue;
            place(layer, &usable);
            if (layer->mapped)
                reserve(&usable, &layer->current);
        }
    }
    wl_list_for_each(layer, &shell->layer_surfaces, link) {
        if (layer->output != output || !layer->initialized || layer->closed || exclusive_edge(&layer->current))
            continue;
        place(layer, layer->current.zone == -1 ? &full : &usable);
    }
    if (memcmp(&usable, &shoutput->work_area, sizeof usable) != 0) {
        shoutput->work_area = usable;
        kiosk_shell_reflow_output(shoutput);
    }
    kiosk_layer_shell_update_focus(shell);
}

static bool valid_state(struct layer_surface *layer, const struct layer_state *s)
{
    if ((!s->width && (s->anchor & (LEFT | RIGHT)) != (LEFT | RIGHT)) ||
        (!s->height && (s->anchor & (TOP | BOTTOM)) != (TOP | BOTTOM))) {
        wl_resource_post_error(layer->resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SIZE,
                               "zero dimensions require opposite anchors");
        return false;
    }
    return true;
}

static void surface_commit(struct wl_listener *listener, void *data)
{
    struct layer_surface *layer = container_of(listener, struct layer_surface, commit_listener);
    struct layer_state state;
    struct layer_commit *commit;
    bool acknowledged;
    bool has_buffer = weston_surface_has_content(layer->surface);
    if (layer->closed)
        return;
    if (wl_list_empty(&layer->commits))
        return; /* Only client-submitted commits carry layer-shell state. */
    commit = container_of(layer->commits.next, struct layer_commit, link);
    state = commit->state;
    acknowledged = commit->acknowledged;
    wl_list_remove(&commit->link);
    free(commit);
    if (has_buffer && (!layer->initialized || !acknowledged)) {
        wl_resource_post_error(layer->resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                               "buffer committed before initial configure was acknowledged");
        return;
    }
    if (layer->mapped && !has_buffer) {
        clear_focus(layer);
        weston_surface_unmap(layer->surface);
        layer->mapped = false;
        layer->initialized = layer->acknowledged = false;
        layer->current = (struct layer_state){ .layer = layer->initial_layer };
        layer->configured_width = layer->configured_height = -1;
        clear_configures(layer);
        kiosk_layer_shell_arrange(layer->shell, layer->output);
        return;
    }
    if (!valid_state(layer, &state))
        return;
    if (layer->mapped && has_buffer &&
        layer->surface_width == layer->surface->width &&
        layer->surface_height == layer->surface->height &&
        memcmp(&layer->current, &state, sizeof layer->current) == 0)
        return; /* Ordinary animation frames do not change shell layout. */
    bool layer_changed = layer->current.layer != state.layer;
    layer->current = state;
    layer->initialized = true;
    if (has_buffer) {
        if (!layer->mapped || layer_changed)
            weston_view_move_to_layer(layer->view,
                &layer->shell->layer_shell_layers[layer->current.layer].view_list);
        if (!layer->mapped) {
            weston_surface_map(layer->surface);
            layer->view->is_mapped = true;
            layer->mapped = true;
        }
    }
    layer->surface_width = layer->surface->width;
    layer->surface_height = layer->surface->height;
    kiosk_layer_shell_arrange(layer->shell, layer->output);
}

/* libweston 14 has an applied-commit signal but no public pre-commit hook.
 * Observe client commit requests through Wayland's public protocol logger API
 * to associate metadata with each queued core transaction. Nothing is logged.
 */
static void protocol_request(void *data, enum wl_protocol_logger_type direction,
                             const struct wl_protocol_logger_message *message)
{
    struct kiosk_shell *shell = data;
    struct weston_surface *surface;
    struct wl_listener *listener;
    struct layer_surface *layer;
    struct layer_commit *commit;
    if (direction != WL_PROTOCOL_LOGGER_REQUEST ||
        strcmp(wl_resource_get_class(message->resource), "wl_surface") ||
        strcmp(message->message->name, "commit"))
        return;
    surface = wl_resource_get_user_data(message->resource);
    listener = wl_signal_get(&surface->destroy_signal, track_destroy);
    if (listener) {
        struct surface_track *track = container_of(listener, struct surface_track, destroy);
        track->committed = true;
    }
    listener = wl_signal_get(&surface->commit_signal, surface_commit);
    if (!listener || shell->layer_shutting_down)
        return;
    layer = container_of(listener, struct layer_surface, commit_listener);
    if (layer->closed)
        return;
    commit = zalloc(sizeof *commit);
    if (!commit) { wl_resource_post_no_memory(layer->resource); return; }
    commit->state = layer->pending;
    commit->acknowledged = layer->acknowledged;
    wl_list_insert(layer->commits.prev, &commit->link);
    if (surface->pending.status & WESTON_SURFACE_DIRTY_BUFFER) {
        bool has_buffer = surface->pending.buffer != NULL;
        if (layer->request_has_buffer && !has_buffer) {
            /* Reset request-side state now, not when its transaction applies. */
            layer->pending = (struct layer_state){ .layer = layer->initial_layer };
            layer->acknowledged = false;
            clear_configures(layer);
        }
        layer->request_has_buffer = has_buffer;
    }
}

static void detach_surface(struct layer_surface *layer)
{
    if (!layer->surface)
        return;
    clear_focus(layer);
    if (layer->mapped)
        weston_surface_unmap(layer->surface);
    layer->mapped = false;
    wl_list_remove(&layer->destroy_listener.link);
    wl_list_remove(&layer->commit_listener.link);
    weston_view_destroy(layer->view);
    layer->view = NULL;
    layer->surface = NULL;
}

static void resource_destroy(struct wl_resource *resource)
{
    struct layer_surface *layer = wl_resource_get_user_data(resource);
    struct kiosk_shell *shell = layer->shell;
    struct weston_output *output = layer->output;
    detach_surface(layer);
    wl_list_remove(&layer->link);
    clear_configures(layer);
    struct layer_commit *commit, *commit_tmp;
    wl_list_for_each_safe(commit, commit_tmp, &layer->commits, link) {
        wl_list_remove(&commit->link);
        free(commit);
    }
    free(layer);
    kiosk_layer_shell_arrange(shell, output);
    if (!shell->layer_shutting_down)
        kiosk_layer_shell_update_focus(shell);
}

static void surface_destroy(struct wl_listener *listener, void *data)
{
    struct layer_surface *layer = container_of(listener, struct layer_surface, destroy_listener);
    layer->closed = true;
    detach_surface(layer);
    zwlr_layer_surface_v1_send_closed(layer->resource);
    kiosk_layer_shell_arrange(layer->shell, layer->output);
}

static struct layer_surface *get_layer(struct wl_resource *resource)
{
    return wl_resource_get_user_data(resource);
}

static void set_size(struct wl_client *client, struct wl_resource *resource, uint32_t w, uint32_t h)
{
    if (w > INT32_MAX || h > INT32_MAX) {
        wl_resource_post_error(resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SIZE, "size exceeds INT32_MAX");
        return;
    }
    get_layer(resource)->pending.width = w;
    get_layer(resource)->pending.height = h;
}

static void set_anchor(struct wl_client *client, struct wl_resource *resource, uint32_t anchor)
{
    if (anchor & ~ANCHORS) {
        wl_resource_post_error(resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_ANCHOR, "invalid anchor bits");
        return;
    }
    get_layer(resource)->pending.anchor = anchor;
}

static void set_zone(struct wl_client *client, struct wl_resource *resource, int32_t zone)
{
    get_layer(resource)->pending.zone = zone;
}

static void set_margin(struct wl_client *client, struct wl_resource *resource,
                       int32_t top, int32_t right, int32_t bottom, int32_t left)
{
    struct layer_state *s = &get_layer(resource)->pending;
    s->top = top; s->right = right; s->bottom = bottom; s->left = left;
}

static void set_keyboard(struct wl_client *client, struct wl_resource *resource, uint32_t keyboard)
{
    if (keyboard > ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND ||
        (keyboard == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND &&
         wl_resource_get_version(resource) < 4)) {
        wl_resource_post_error(resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_KEYBOARD_INTERACTIVITY,
                               "invalid keyboard mode for this version");
        return;
    }
    get_layer(resource)->pending.keyboard = keyboard;
}

static void set_layer(struct wl_client *client, struct wl_resource *resource, uint32_t level)
{
    if (level > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
        wl_resource_post_error(resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE, "invalid layer");
        return;
    }
    get_layer(resource)->pending.layer = level;
}

static void ack_configure(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
    struct layer_surface *layer = get_layer(resource);
    struct layer_configure *config, *tmp;
    bool found = false;
    wl_list_for_each(config, &layer->configures, link)
        if (config->serial == serial) { found = true; break; }
    if (!found) {
        wl_resource_post_error(resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                               "unknown or already acknowledged configure serial");
        return;
    }
    wl_list_for_each_safe(config, tmp, &layer->configures, link) {
        bool last = config->serial == serial;
        wl_list_remove(&config->link);
        free(config);
        if (last) break;
    }
    layer->acknowledged = true;
}

static void destroy_request(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void get_popup(struct wl_client *client, struct wl_resource *resource, struct wl_resource *popup)
{
    /* libweston-desktop 14 rejects parentless XDG popups before this request
     * can run. Supporting them requires an upstream desktop API extension. */
    wl_resource_post_error(resource, ZWLR_LAYER_SURFACE_V1_ERROR_INVALID_SURFACE_STATE,
                           "layer-shell XDG popups are not supported by libweston-desktop 14");
}

static const struct zwlr_layer_surface_v1_interface surface_impl = {
    .set_size = set_size,
    .set_anchor = set_anchor,
    .set_exclusive_zone = set_zone,
    .set_margin = set_margin,
    .set_keyboard_interactivity = set_keyboard,
    .get_popup = get_popup,
    .ack_configure = ack_configure,
    .destroy = destroy_request,
    .set_layer = set_layer,
};

static void get_layer_surface(struct wl_client *client, struct wl_resource *resource,
                              uint32_t id, struct wl_resource *surface_resource,
                              struct wl_resource *output_resource, uint32_t level, const char *namespace)
{
    struct kiosk_shell *shell = wl_resource_get_user_data(resource);
    struct weston_surface *surface = wl_resource_get_user_data(surface_resource);
    struct surface_track *track;
    struct weston_output *output = NULL;
    struct layer_surface *layer;
    if (level > ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
        wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_INVALID_LAYER, "invalid layer");
        return;
    }
    if (surface->role_name || surface->committed || surface->committed_private) {
        wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_ROLE, "wl_surface already has a role");
        return;
    }
    bool committed = false;
    wl_list_for_each(track, &shell->layer_surface_tracks, link)
        if (track->surface == surface) committed = track->committed;
    if (committed || weston_surface_has_content(surface) || surface->pending.buffer) {
        wl_resource_post_error(resource, ZWLR_LAYER_SHELL_V1_ERROR_ALREADY_CONSTRUCTED,
                               "wl_surface has a buffer or was already committed");
        return;
    }
    if (output_resource) {
        struct weston_head *head = weston_head_from_resource(output_resource);
        if (head) output = head->output;
    } else {
        output = weston_shell_utils_get_default_output(shell->compositor);
    }
    layer = zalloc(sizeof *layer);
    if (!layer) { wl_client_post_no_memory(client); return; }
    layer->resource = wl_resource_create(client, &zwlr_layer_surface_v1_interface,
                                          wl_resource_get_version(resource), id);
    if (!layer->resource) { free(layer); wl_client_post_no_memory(client); return; }
    layer->view = weston_view_create(surface);
    if (!layer->view) {
        wl_resource_destroy(layer->resource);
        free(layer);
        wl_client_post_no_memory(client);
        return;
    }
    layer->shell = shell;
    layer->surface = surface;
    layer->output = output;
    layer->initial_layer = level;
    layer->current.layer = layer->pending.layer = level;
    layer->configured_width = layer->configured_height = -1;
    wl_list_init(&layer->configures);
    wl_list_init(&layer->commits);
    wl_list_insert(&shell->layer_surfaces, &layer->link);
    layer->destroy_listener.notify = surface_destroy;
    layer->commit_listener.notify = surface_commit;
    wl_signal_add(&surface->destroy_signal, &layer->destroy_listener);
    wl_signal_add(&surface->commit_signal, &layer->commit_listener);
    weston_surface_set_role(surface, "zwlr_layer_surface_v1", resource, ZWLR_LAYER_SHELL_V1_ERROR_ROLE);
    wl_resource_set_implementation(layer->resource, &surface_impl, layer, resource_destroy);
    if (!output) {
        layer->closed = true;
        zwlr_layer_surface_v1_send_closed(layer->resource);
    }
}

static const struct zwlr_layer_shell_v1_interface shell_impl = {
    .get_layer_surface = get_layer_surface,
    .destroy = destroy_request,
};

static void bind_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *resource = wl_resource_create(client, &zwlr_layer_shell_v1_interface,
                                                       MIN(version, LAYER_VERSION), id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &shell_impl, data, NULL);
}

void kiosk_layer_shell_output_removed(struct kiosk_shell *shell, struct weston_output *output)
{
    struct layer_surface *layer;
    wl_list_for_each(layer, &shell->layer_surfaces, link) {
        if (layer->output != output)
            continue;
        layer->closed = true;
        clear_focus(layer);
        if (layer->mapped && layer->surface)
            weston_surface_unmap(layer->surface);
        layer->mapped = false;
        layer->output = NULL;
        zwlr_layer_surface_v1_send_closed(layer->resource);
    }
    if (!shell->layer_shutting_down)
        kiosk_layer_shell_update_focus(shell);
}

int kiosk_layer_shell_init(struct kiosk_shell *shell)
{
    static const enum weston_layer_position positions[] = {
        WESTON_LAYER_POSITION_BACKGROUND + 1,
        WESTON_LAYER_POSITION_BOTTOM_UI,
        WESTON_LAYER_POSITION_TOP_UI,
        WESTON_LAYER_POSITION_TOP_UI + 1,
    };
    wl_list_init(&shell->layer_surfaces);
    wl_list_init(&shell->layer_surface_tracks);
    for (int i = 0; i < 4; ++i) {
        weston_layer_init(&shell->layer_shell_layers[i], shell->compositor);
        weston_layer_set_position(&shell->layer_shell_layers[i], positions[i]);
    }
    shell->layer_surface_created.notify = surface_created;
    wl_signal_add(&shell->compositor->create_surface_signal, &shell->layer_surface_created);
    shell->layer_global = wl_global_create(shell->compositor->wl_display,
        &zwlr_layer_shell_v1_interface, LAYER_VERSION, shell, bind_shell);
    shell->layer_protocol_logger = wl_display_add_protocol_logger(
        shell->compositor->wl_display, protocol_request, shell);
    return shell->layer_global && shell->layer_protocol_logger ? 0 : -1;
}

void kiosk_layer_shell_fini(struct kiosk_shell *shell)
{
    struct layer_surface *layer, *tmp;
    struct surface_track *track, *track_tmp;
    shell->layer_shutting_down = true;
    if (shell->layer_protocol_logger)
        wl_protocol_logger_destroy(shell->layer_protocol_logger);
    if (shell->layer_global) wl_global_destroy(shell->layer_global);
    wl_list_remove(&shell->layer_surface_created.link);
    wl_list_for_each_safe(layer, tmp, &shell->layer_surfaces, link)
        wl_resource_destroy(layer->resource);
    wl_list_for_each_safe(track, track_tmp, &shell->layer_surface_tracks, link)
        track_destroy(&track->destroy, NULL);
    for (int i = 0; i < 4; ++i)
        weston_layer_fini(&shell->layer_shell_layers[i]);
}
