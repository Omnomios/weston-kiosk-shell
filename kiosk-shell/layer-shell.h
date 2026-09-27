/* SPDX-License-Identifier: MIT */
#ifndef KIOSK_LAYER_SHELL_H
#define KIOSK_LAYER_SHELL_H

#include "kiosk-shell.h"

int kiosk_layer_shell_init(struct kiosk_shell *shell);
void kiosk_layer_shell_fini(struct kiosk_shell *shell);
void kiosk_layer_shell_arrange(struct kiosk_shell *shell, struct weston_output *output);
void kiosk_layer_shell_output_removed(struct kiosk_shell *shell, struct weston_output *output);
void kiosk_layer_shell_update_focus(struct kiosk_shell *shell);
bool kiosk_layer_shell_activate(struct kiosk_shell *shell, struct weston_view *view,
                               struct weston_seat *seat, uint32_t flags);
void kiosk_shell_reflow_output(struct kiosk_shell_output *output);

#endif
