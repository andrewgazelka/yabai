extern struct event_loop g_event_loop;
extern struct process_manager g_process_manager;
extern struct display_manager g_display_manager;
extern struct space_manager g_space_manager;
extern struct window_manager g_window_manager;
extern int g_connection;

struct virtual_space_manager g_virtual_space_manager;

#define vs_buf_remove(b, idx) (memmove(&(b)[idx], &(b)[(idx)+1], sizeof(*(b)) * (buf_len(b) - (idx) - 1)), buf__hdr(b)->len--)

static inline bool virtual_space_display_is_active(uint32_t did, uint32_t *display_list, int display_count)
{
    for (int i = 0; i < display_count; ++i) {
        if (display_list[i] == did) return true;
    }
    return false;
}

static struct virtual_display *virtual_space_find_display(uint32_t did)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    for (int i = 0; i < buf_len(vsm->displays); ++i) {
        if (vsm->displays[i].did == did) return &vsm->displays[i];
    }
    return NULL;
}

//
// NOTE: The native space each display keeps. Its windows are the windows of the virtual spaces;
// while the display shows another native space (a fullscreen space, or an extra space the user
// swiped to) the virtual views must not be validated against that space's window list.
//
static uint64_t virtual_space_home_for_display(uint32_t did)
{
    struct virtual_display *vd = virtual_space_find_display(did);
    if (vd && vd->home_sid) return vd->home_sid;
    return display_space_id_real(did);
}

static uint64_t virtual_space_pick_home(uint32_t did)
{
    uint64_t sid = display_space_id_real(did);
    if (sid && space_is_user(sid)) return sid;

    int count = 0;
    uint64_t *space_list = display_space_list_real(did, &count);
    for (int i = 0; i < count; ++i) {
        if (space_is_user(space_list[i])) return space_list[i];
    }

    return sid;
}

bool virtual_space_can_validate(uint64_t sid)
{
    struct virtual_space *vs = virtual_space_find(sid);
    if (!vs) return false;

    struct virtual_display *vd = virtual_space_find_display(vs->did);
    if (!vd) return false;

    return !vd->home_sid || display_space_id_real(vs->did) == vd->home_sid;
}

static uint32_t virtual_space_current_display(void)
{
    uint32_t did = g_display_manager.current_display_id;
    if (!did || !virtual_space_find_display(did)) did = display_manager_active_display_id();
    if (!did || !virtual_space_find_display(did)) did = display_manager_main_display_id();
    return did;
}

bool virtual_space_enabled(void)
{
    return g_virtual_space_manager.enabled;
}

struct virtual_space *virtual_space_find(uint64_t sid)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    if (!vsm->enabled || !virtual_space_is_virtual(sid)) return NULL;

    for (int i = 0; i < buf_len(vsm->spaces); ++i) {
        if (vsm->spaces[i].sid == sid) return &vsm->spaces[i];
    }
    return NULL;
}

int virtual_space_index(uint64_t sid)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    for (int i = 0; i < buf_len(vsm->spaces); ++i) {
        if (vsm->spaces[i].sid == sid) return i + 1;
    }
    return 0;
}

uint64_t virtual_space_at_index(int index)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    if (index < 1 || index > buf_len(vsm->spaces)) return 0;
    return vsm->spaces[index-1].sid;
}

uint64_t virtual_space_prev(uint64_t sid)
{
    int index = virtual_space_index(sid);
    return index > 1 ? virtual_space_at_index(index - 1) : 0;
}

uint64_t virtual_space_next(uint64_t sid)
{
    int index = virtual_space_index(sid);
    return index ? virtual_space_at_index(index + 1) : 0;
}

uint64_t virtual_space_first(void)
{
    return virtual_space_at_index(1);
}

uint64_t virtual_space_last(void)
{
    return virtual_space_at_index(buf_len(g_virtual_space_manager.spaces));
}

//
// NOTE: Maps a native space id (from a rule or label made before virtual spaces were turned on)
// to the virtual space with the same mission-control index. Never hands a native space id back
// while virtual spaces are on, so callers can't end up moving windows between native spaces.
//
uint64_t virtual_space_translate(uint64_t sid)
{
    if (!g_virtual_space_manager.enabled || !sid) return sid;
    if (virtual_space_is_virtual(sid)) return virtual_space_find(sid) ? sid : 0;
    if (!space_is_user(sid)) return sid;
    return virtual_space_at_index(space_manager_mission_control_index(sid));
}

uint32_t virtual_space_display(uint64_t sid)
{
    struct virtual_space *vs = virtual_space_find(sid);
    return vs ? vs->did : 0;
}

bool virtual_space_is_visible(uint64_t sid)
{
    struct virtual_space *vs = virtual_space_find(sid);
    if (!vs) return false;

    struct virtual_display *vd = virtual_space_find_display(vs->did);
    return vd && vd->sid == sid;
}

uint64_t virtual_space_active_for_display(uint32_t did)
{
    struct virtual_display *vd = virtual_space_find_display(did);
    return vd ? vd->sid : 0;
}

//
// NOTE: The space a display shows: its virtual space while the display is on its real user space,
// or the real space itself while it shows something else (a native fullscreen space).
//
uint64_t virtual_space_display_space(uint32_t did, uint64_t real_sid)
{
    if (!real_sid || !space_is_user(real_sid)) return real_sid;

    uint64_t sid = virtual_space_active_for_display(did);
    return sid ? sid : real_sid;
}

uint64_t *virtual_space_list_for_display(uint32_t did, int *count)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;

    *count = 0;
    int space_count = buf_len(vsm->spaces);
    if (!space_count) return NULL;

    uint64_t *space_list = ts_alloc_list(uint64_t, space_count);
    for (int i = 0; i < space_count; ++i) {
        if (vsm->spaces[i].did == did) {
            space_list[(*count)++] = vsm->spaces[i].sid;
        }
    }

    return *count ? space_list : NULL;
}

uint64_t virtual_space_window_space(uint32_t wid)
{
    struct window *window = window_manager_find_window(&g_window_manager, wid);

    if (window) {
        if (window_check_flag(window, WINDOW_FULLSCREEN)) {
            window->vsid = 0;
            window->vs_hidden = false;
        } else if (window->vsid) {
            if (virtual_space_find(window->vsid)) return window->vsid;
            window->vsid = 0;
        }
    }

    uint64_t real_sid = window_space_real(wid);
    if (!real_sid || !space_is_user(real_sid)) return real_sid;

    uint64_t sid = virtual_space_active_for_display(space_display_id(real_sid));
    if (!sid) return real_sid;

    if (window && !window_check_flag(window, WINDOW_FULLSCREEN)) window->vsid = sid;
    return sid;
}

uint32_t *virtual_space_window_list(uint64_t *space_list, int space_count, int cid, int *count, bool include_minimized)
{
    int real_count = 0;
    uint64_t real_list[space_count > 0 ? space_count : 1];

    for (int i = 0; i < space_count; ++i) {
        uint64_t real_sid = space_list[i];

        if (virtual_space_is_virtual(real_sid)) {
            struct virtual_space *vs = virtual_space_find(real_sid);
            real_sid = vs ? virtual_space_home_for_display(vs->did) : 0;
        }

        if (!real_sid) continue;

        bool duplicate = false;
        for (int j = 0; j < real_count; ++j) {
            if (real_list[j] == real_sid) { duplicate = true; break; }
        }

        if (!duplicate) real_list[real_count++] = real_sid;
    }

    *count = 0;
    if (!real_count) return NULL;

    int window_count = 0;
    uint32_t *window_list = space_window_list_for_connection_real(real_list, real_count, cid, &window_count, include_minimized);
    if (!window_list) return NULL;

    for (int i = 0; i < window_count; ++i) {
        uint64_t sid = virtual_space_window_space(window_list[i]);

        for (int j = 0; j < space_count; ++j) {
            if (space_list[j] == sid) {
                window_list[(*count)++] = window_list[i];
                break;
            }
        }
    }

    return window_list;
}

bool virtual_space_window_is_hidden(struct window *window)
{
    return g_virtual_space_manager.enabled && window->vs_hidden;
}

//
// NOTE: Frames are kept relative to the display they were on (like AeroSpace), so a floating
// window comes back in the same place when its virtual space is shown on another display.
//
static void virtual_space_save_frame(struct window *window, CGRect frame)
{
    if (frame.size.width <= 0 || frame.size.height <= 0) return;

    CGPoint center = { CGRectGetMidX(frame), CGRectGetMidY(frame) };
    uint32_t did = display_manager_point_display_id(center);
    if (!did) did = window_display_id_real(window->id);
    if (!did) did = display_manager_main_display_id();

    CGRect bounds = CGDisplayBounds(did);
    window->vs_origin = (CGPoint) {
        (frame.origin.x - bounds.origin.x) / bounds.size.width,
        (frame.origin.y - bounds.origin.y) / bounds.size.height
    };
    window->vs_size = frame.size;
    window->vs_has_frame = true;
}

static CGRect virtual_space_saved_frame_on_display(struct window *window, uint32_t did)
{
    CGRect bounds = CGDisplayBounds(did);
    CGSize size = window->vs_size;

    CGPoint origin = {
        bounds.origin.x + window->vs_origin.x * bounds.size.width,
        bounds.origin.y + window->vs_origin.y * bounds.size.height
    };

    if (origin.x + size.width  > CGRectGetMaxX(bounds)) origin.x = CGRectGetMaxX(bounds) - size.width;
    if (origin.y + size.height > CGRectGetMaxY(bounds)) origin.y = CGRectGetMaxY(bounds) - size.height;
    if (origin.x < bounds.origin.x) origin.x = bounds.origin.x;
    if (origin.y < bounds.origin.y) origin.y = bounds.origin.y;

    return (CGRect) { origin, size };
}

void virtual_space_save_requested_frame(struct window *window, float x, float y, float w, float h)
{
    virtual_space_save_frame(window, CGRectMake(x, y, w, h));
}

static inline int virtual_space_count_displays_containing(CGPoint point, uint32_t *display_list, int display_count)
{
    int result = 0;
    for (int i = 0; i < display_count; ++i) {
        if (CGRectContainsPoint(CGDisplayBounds(display_list[i]), point)) ++result;
    }
    return result;
}

//
// NOTE: AeroSpace's corner choice (refresh.swift, layoutWorkspaces): hide in the bottom-right corner
// unless another display touches that corner, in which case the 1px sliver would land on it.
//
static CGPoint virtual_space_hide_point(uint32_t did, CGSize size)
{
    int display_count = 0;
    uint32_t *display_list = display_manager_active_display_list(&display_count);

    CGRect bounds = CGDisplayBounds(did);
    float x_off = bounds.size.width  * 0.1f;
    float y_off = bounds.size.height * 0.1f;
    float max_x = CGRectGetMaxX(bounds);
    float max_y = CGRectGetMaxY(bounds);
    float min_x = CGRectGetMinX(bounds);

    int brc = virtual_space_count_displays_containing(CGPointMake(max_x + 2, max_y - y_off), display_list, display_count)
            + virtual_space_count_displays_containing(CGPointMake(max_x - x_off, max_y + 2), display_list, display_count)
            + 10 * virtual_space_count_displays_containing(CGPointMake(max_x + 2, max_y + 2), display_list, display_count);

    int blc = virtual_space_count_displays_containing(CGPointMake(min_x - 2, max_y - y_off), display_list, display_count)
            + virtual_space_count_displays_containing(CGPointMake(min_x + x_off, max_y + 2), display_list, display_count)
            + 10 * virtual_space_count_displays_containing(CGPointMake(min_x - 2, max_y + 2), display_list, display_count);

    CGRect visible = display_bounds_constrained(did, true);
    if (blc < brc) {
        return CGPointMake(CGRectGetMinX(visible) + 1 - size.width, CGRectGetMaxY(visible) - 1);
    }

    return CGPointMake(CGRectGetMaxX(visible) - 1, CGRectGetMaxY(visible) - 1);
}

static void virtual_space_set_window_position(struct window *window, CGPoint point)
{
    AX_ENHANCED_UI_WORKAROUND(window->application->ref, {
        CFTypeRef position_ref = AXValueCreate(kAXValueTypeCGPoint, (void *) &point);
        if (position_ref) {
            AXUIElementSetAttributeValue(window->ref, kAXPositionAttribute, position_ref);
            CFRelease(position_ref);
        }
    });
}

static void virtual_space_hide_window(struct window *window, uint32_t did)
{
    if (window_check_flag(window, WINDOW_MINIMIZE)) return;
    if (window_check_flag(window, WINDOW_FULLSCREEN)) return;

    if (!window->vs_hidden) {
        virtual_space_save_frame(window, window_ax_frame(window));
        window->vs_hidden = true;
    }

    CGSize size = window->vs_has_frame ? window->vs_size : window->frame.size;
    virtual_space_set_window_position(window, virtual_space_hide_point(did, size));
}

static void virtual_space_unhide_window(struct window *window, uint32_t did, bool restore_frame)
{
    if (!window->vs_hidden) return;
    window->vs_hidden = false;

    if (restore_frame && window->vs_has_frame) {
        CGRect frame = virtual_space_saved_frame_on_display(window, did);
        window_manager_set_window_frame_raw(window, frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
    }
}

static void virtual_space_show_on_display(struct virtual_space *vs, uint32_t did)
{
    struct virtual_display *vd = virtual_space_find_display(did);
    if (!vd || vd->sid == vs->sid) return;
    if (virtual_space_is_visible(vs->sid)) return;

    uint64_t old_sid = vd->sid;

    float animation_duration = g_window_manager.window_animation_duration;
    g_window_manager.window_animation_duration = 0.0f;
    SLSDisableUpdate(g_connection);

    vd->sid = vs->sid;
    vs->did = did;

    //
    // NOTE: Show the new space first and hide the old one after (AeroSpace does the same), so the
    // display is never empty in between.
    //

    table_for (struct window *window, g_window_manager.window, {
        if (window->vsid == vs->sid && window->vs_hidden) {
            bool managed = window_manager_find_managed_window(&g_window_manager, window) != NULL;
            virtual_space_unhide_window(window, did, !managed);
        }
    })

    struct view *view = space_manager_find_view(&g_space_manager, vs->sid);
    if (view->layout != VIEW_FLOAT) {
        view_update(view);
        view_set_flag(view, VIEW_IS_DIRTY);
        window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, vs->sid);
        if (view_is_dirty(view)) {
            window_node_flush(view->root);
            view_clear_flag(view, VIEW_IS_DIRTY);
        }
    }

    if (old_sid) {
        table_for (struct window *window, g_window_manager.window, {
            if (window->vsid == old_sid) virtual_space_hide_window(window, did);
        })
    }

    SLSReenableUpdate(g_connection);
    g_window_manager.window_animation_duration = animation_duration;
}

void virtual_space_suppress_finder_focus(void)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;

    pid_t pid = 0;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    GetProcessPID(&g_process_manager.finder_psn, &pid);
#pragma clang diagnostic pop

    vsm->suppress_pid = pid;
    vsm->suppress_until = read_os_timer() + read_os_freq();
}

//
// NOTE: An empty space has nothing to focus. Activate Finder without raising its windows (what yabai
// does when the last window leaves a space) and ignore the focus events that follow for a second, so
// a Finder window on a hidden space does not pull us back to it.
//
static void virtual_space_focus_nothing(void)
{
    virtual_space_suppress_finder_focus();
    _SLPSSetFrontProcessWithOptions(&g_process_manager.finder_psn, 0, kCPSNoWindows);
}

static void virtual_space_focus_window_in(struct virtual_space *vs);

void virtual_space_note_focus_lost(void)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    if (!vsm->enabled) return;

    vsm->leave_until = read_os_timer() + read_os_freq() / 2;
}

//
// NOTE: Focus that lands on a window of a hidden virtual space normally shows that space. Two
// cases do not: Finder activated by us because a space was empty, and macOS picking the next key
// window by itself right after a window was closed, minimized, or its app hidden or quit. In the
// second case focus goes back to the space the display shows, unless the user cmd-tabbed.
//
bool virtual_space_should_ignore_focus(struct window *window, bool explicit_switch)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    if (!vsm->enabled) return false;

    uint64_t sid = virtual_space_window_space(window->id);
    if (!virtual_space_is_virtual(sid) || virtual_space_is_visible(sid)) return false;

    uint64_t now = read_os_timer();

    if (vsm->suppress_pid) {
        if (now > vsm->suppress_until) {
            vsm->suppress_pid = 0;
        } else if (window->application->pid == vsm->suppress_pid) {
            return true;
        }
    }

    if (!explicit_switch && vsm->leave_until && now <= vsm->leave_until) {
        vsm->leave_until = 0;

        struct virtual_space *vs = virtual_space_find(virtual_space_active_for_display(virtual_space_current_display()));
        if (vs) virtual_space_focus_window_in(vs);
        return true;
    }

    return false;
}

static void virtual_space_focus_window_in(struct virtual_space *vs)
{
    struct window *window = vs->last_wid ? window_manager_find_window(&g_window_manager, vs->last_wid) : NULL;
    if (window && (window->vsid != vs->sid || window_check_flag(window, WINDOW_MINIMIZE) || window->application->is_hidden)) {
        window = NULL;
    }

    if (!window) window = window_manager_find_window_on_space_by_rank_filtering_window(&g_window_manager, vs->sid, 1, 0);

    if (window) {
        window->frame = window_ax_frame(window);
        window_manager_focus_window_with_raise(&window->application->psn, window->id, window->ref);
    } else {
        virtual_space_focus_nothing();
    }
}

static void virtual_space_did_change_space(uint64_t sid)
{
    if (g_space_manager.current_space_id == sid) return;

    g_space_manager.last_space_id = g_space_manager.current_space_id;
    g_space_manager.current_space_id = sid;
    event_signal_push(SIGNAL_SPACE_CHANGED, NULL);
}

enum space_op_error virtual_space_focus(uint64_t sid)
{
    struct virtual_space *vs = virtual_space_find(sid);
    if (!vs) return SPACE_OP_ERROR_MISSING_DST;

    uint64_t cur_sid = space_manager_active_space();
    if (cur_sid == sid) return SPACE_OP_ERROR_SAME_SPACE;

    if (virtual_space_is_visible(sid)) {
        //
        // NOTE: The space is shown on another display; focus that display. The DISPLAY_CHANGED
        // event updates the current space and fires the signals.
        //
        display_manager_focus_display(vs->did, sid);
        return SPACE_OP_ERROR_SUCCESS;
    }

    uint32_t did = virtual_space_display(cur_sid);
    if (!did || !virtual_space_find_display(did)) did = virtual_space_current_display();

    virtual_space_show_on_display(vs, did);
    virtual_space_focus_window_in(vs);
    virtual_space_did_change_space(sid);

    return SPACE_OP_ERROR_SUCCESS;
}

enum space_op_error virtual_space_focus_on_display(uint64_t sid, uint32_t did)
{
    struct virtual_space *vs = virtual_space_find(sid);
    if (!vs) return SPACE_OP_ERROR_MISSING_DST;
    if (!virtual_space_find_display(did)) return SPACE_OP_ERROR_MISSING_DST;

    if (virtual_space_is_visible(sid)) {
        if (vs->did != did) return SPACE_OP_ERROR_SAME_DISPLAY;
        if (virtual_space_current_display() == did) virtual_space_focus(sid);
        return SPACE_OP_ERROR_SUCCESS;
    }

    bool had_focus = virtual_space_current_display() == did;
    virtual_space_show_on_display(vs, did);

    if (had_focus) {
        virtual_space_focus_window_in(vs);
        virtual_space_did_change_space(sid);
    } else {
        event_signal_push(SIGNAL_SPACE_CHANGED, NULL);
    }

    return SPACE_OP_ERROR_SUCCESS;
}

enum space_op_error virtual_space_create(uint32_t did)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    if (!vsm->enabled) return SPACE_OP_ERROR_MISSING_SRC;
    if (!did || !virtual_space_find_display(did)) did = virtual_space_current_display();
    if (vsm->next_sid >= VIRTUAL_SPACE_SID_LIMIT) return SPACE_OP_ERROR_INVALID_SRC;

    struct virtual_space vs = { .sid = vsm->next_sid++, .did = did, .last_wid = 0 };
    buf_push(vsm->spaces, vs);
    space_manager_find_view(&g_space_manager, vs.sid);

    event_signal_push(SIGNAL_SPACE_CREATED, (void *)(uintptr_t) vs.sid);
    return SPACE_OP_ERROR_SUCCESS;
}

enum space_op_error virtual_space_destroy(uint64_t sid)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;

    int index = virtual_space_index(sid);
    if (!index) return SPACE_OP_ERROR_MISSING_SRC;
    if (buf_len(vsm->spaces) <= buf_len(vsm->displays)) return SPACE_OP_ERROR_INVALID_SRC;

    uint32_t did = vsm->spaces[index-1].did;
    uint64_t dst_sid = 0;

    if (virtual_space_is_visible(sid)) {
        struct virtual_space *replacement = NULL;

        for (int i = index; i < buf_len(vsm->spaces) && !replacement; ++i) {
            if (!virtual_space_is_visible(vsm->spaces[i].sid)) replacement = &vsm->spaces[i];
        }

        for (int i = index - 2; i >= 0 && !replacement; --i) {
            if (!virtual_space_is_visible(vsm->spaces[i].sid)) replacement = &vsm->spaces[i];
        }

        if (!replacement) return SPACE_OP_ERROR_INVALID_SRC;

        bool had_focus = space_manager_active_space() == sid;
        virtual_space_show_on_display(replacement, did);
        dst_sid = replacement->sid;

        if (had_focus) {
            virtual_space_focus_window_in(replacement);
            virtual_space_did_change_space(dst_sid);
        }
    } else {
        dst_sid = virtual_space_active_for_display(did);
        if (!dst_sid) dst_sid = virtual_space_active_for_display(virtual_space_current_display());
    }

    //
    // NOTE: Like macOS destroying a space, the windows are not closed; they move to the space the
    // display shows now.
    //

    int window_count = 0;
    struct window **window_list = ts_alloc_list(struct window *, g_window_manager.window.count);
    table_for (struct window *window, g_window_manager.window, {
        if (window->vsid == sid) window_list[window_count++] = window;
    })

    for (int i = 0; i < window_count; ++i) {
        window_manager_send_window_to_space(&g_space_manager, &g_window_manager, window_list[i], dst_sid, true);
    }

    struct view *view = table_find(&g_space_manager.view, &sid);
    if (view) {
        table_remove(&g_space_manager.view, &sid);
        view_clear(view);
        view_destroy(view);
        free(view);
    }

    space_manager_remove_label_for_space(&g_space_manager, sid);
    if (g_space_manager.last_space_id == sid) g_space_manager.last_space_id = g_space_manager.current_space_id;

    index = virtual_space_index(sid);
    if (index) vs_buf_remove(vsm->spaces, index - 1);

    event_signal_push(SIGNAL_SPACE_DESTROYED, (void *)(uintptr_t) sid);
    return SPACE_OP_ERROR_SUCCESS;
}

void virtual_space_move_window(struct window *window, uint64_t sid)
{
    struct virtual_space *dst = virtual_space_find(sid);
    if (!dst) return;

    uint64_t src_sid = virtual_space_window_space(window->id);
    if (src_sid == sid) return;

    bool was_hidden = window->vs_hidden;
    uint32_t src_did = was_hidden ? virtual_space_display(src_sid) : window_display_id_real(window->id);

    window->vsid = sid;

    if (!virtual_space_is_visible(sid)) {
        virtual_space_hide_window(window, dst->did);
    } else if (was_hidden) {
        virtual_space_unhide_window(window, dst->did, true);
    } else if (src_did && src_did != dst->did && !window_check_flag(window, WINDOW_MINIMIZE)) {
        virtual_space_save_frame(window, window_ax_frame(window));
        CGRect frame = virtual_space_saved_frame_on_display(window, dst->did);
        window_manager_set_window_frame_raw(window, frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
    }
}

//
// NOTE: Something activated a window (cmd-tab, a dock click, `window --focus`, a notification). If it
// lives on a hidden virtual space, show that space, like AeroSpace does.
//
void virtual_space_window_did_receive_focus(struct window *window)
{
    if (!g_virtual_space_manager.enabled) return;

    uint64_t sid = virtual_space_window_space(window->id);
    struct virtual_space *vs = virtual_space_find(sid);
    if (!vs) return;

    if (!virtual_space_is_visible(sid)) {
        uint32_t did = virtual_space_find_display(vs->did) ? vs->did : virtual_space_current_display();
        virtual_space_show_on_display(vs, did);
        virtual_space_did_change_space(sid);

        //
        // NOTE: The cached frame still points at the corner; mouse_follows_focus reads it next.
        //
        window->frame = window_ax_frame(window);
    }

    vs->last_wid = window->id;
}

//
// NOTE: A floating window dragged to another display joins the space that display shows.
//
void virtual_space_window_did_move(struct window *window)
{
    if (!g_virtual_space_manager.enabled) return;
    if (!window->vsid) return;

    if (window->vs_hidden) {
        //
        // NOTE: The app moved a hidden window back on screen (restoring its frame, re-showing a
        // panel). Put it back in the corner; the frame it asked for is what it gets when shown.
        //
        uint32_t did = virtual_space_display(window->vsid);
        if (!did) return;

        CGRect frame = window_ax_frame(window);
        CGRect overlap = CGRectIntersection(frame, CGDisplayBounds(did));
        if (CGRectIsNull(overlap) || overlap.size.width < 40 || overlap.size.height < 40) return;

        virtual_space_save_frame(window, frame);
        virtual_space_hide_window(window, did);
        return;
    }

    if (window_manager_find_managed_window(&g_window_manager, window)) return;

    struct virtual_space *vs = virtual_space_find(window->vsid);
    if (!vs) return;

    uint32_t did = window_display_id_real(window->id);
    if (!did || did == vs->did) return;

    uint64_t sid = virtual_space_active_for_display(did);
    if (sid) window->vsid = sid;
}

void virtual_space_window_did_deminimize(struct window *window)
{
    if (!g_virtual_space_manager.enabled) return;
    if (window_check_flag(window, WINDOW_FULLSCREEN)) return;

    uint32_t did = window_display_id_real(window->id);
    uint64_t sid = virtual_space_active_for_display(did);
    if (!sid) sid = virtual_space_active_for_display(virtual_space_current_display());

    window->vsid = sid;
    if (window->vs_hidden && did) {
        virtual_space_unhide_window(window, did, true);
    } else {
        window->vs_hidden = false;
    }
}

void virtual_space_application_did_unhide(struct application *application)
{
    if (!g_virtual_space_manager.enabled) return;

    table_for (struct window *window, g_window_manager.window, {
        if (window->application == application && window->vsid && !virtual_space_is_visible(window->vsid)) {
            uint32_t did = virtual_space_display(window->vsid);
            if (did) virtual_space_hide_window(window, did);
        }
    })
}

void virtual_space_rehide_all(void)
{
    if (!g_virtual_space_manager.enabled) return;

    table_for (struct window *window, g_window_manager.window, {
        if (window->vsid && !virtual_space_is_visible(window->vsid)) {
            uint32_t did = virtual_space_display(window->vsid);
            if (did) virtual_space_hide_window(window, did);
        }
    })
}

void virtual_space_displays_changed(void)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    if (!vsm->enabled) return;

    int display_count = 0;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list || !display_count) return;

    for (int i = 0; i < buf_len(vsm->displays);) {
        if (!virtual_space_display_is_active(vsm->displays[i].did, display_list, display_count)) {
            vs_buf_remove(vsm->displays, i);
        } else {
            ++i;
        }
    }

    uint32_t main_did = display_manager_main_display_id();
    for (int i = 0; i < buf_len(vsm->spaces); ++i) {
        if (!virtual_space_display_is_active(vsm->spaces[i].did, display_list, display_count)) {
            vsm->spaces[i].did = main_did;
        }
    }

    for (int i = 0; i < display_count; ++i) {
        uint32_t did = display_list[i];
        if (virtual_space_find_display(did)) continue;

        int pick = -1;
        for (int j = 0; j < buf_len(vsm->spaces) && pick == -1; ++j) {
            if (vsm->spaces[j].did == did && !virtual_space_is_visible(vsm->spaces[j].sid)) pick = j;
        }

        for (int j = 0; j < buf_len(vsm->spaces) && pick == -1; ++j) {
            if (!virtual_space_is_visible(vsm->spaces[j].sid)) pick = j;
        }

        if (pick == -1) {
            virtual_space_create(did);
            pick = buf_len(vsm->spaces) - 1;
        }

        buf_push(vsm->displays, ((struct virtual_display) { .did = did, .sid = 0, .home_sid = virtual_space_pick_home(did) }));
        virtual_space_show_on_display(&vsm->spaces[pick], did);
    }

    for (int i = 0; i < buf_len(vsm->spaces); ++i) {
        space_manager_mark_view_invalid(&g_space_manager, vsm->spaces[i].sid);
    }

    //
    // NOTE: Hidden windows sat in the corner of a display that may have moved or gone; put them in
    // the corner of the display their space belongs to now. Windows whose space was visible on a
    // removed display are now on a hidden space and get hidden here too.
    //
    virtual_space_rehide_all();

    for (int i = 0; i < buf_len(vsm->displays); ++i) {
        space_manager_refresh_view(&g_space_manager, vsm->displays[i].sid);
    }

    g_space_manager.current_space_id = space_manager_active_space();
}

void virtual_space_serialize_display_spaces(FILE *rsp, uint32_t did)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;

    bool first = true;
    fprintf(rsp, "\t\"spaces\":[");
    for (int i = 0; i < buf_len(vsm->spaces); ++i) {
        if (vsm->spaces[i].did != did) continue;
        fprintf(rsp, first ? "%d" : ", %d", i + 1);
        first = false;
    }
    fprintf(rsp, "]");
}

bool virtual_space_query_spaces(FILE *rsp, uint64_t flags)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;

    fprintf(rsp, "[");
    for (int i = 0; i < buf_len(vsm->spaces); ++i) {
        struct view *view = space_manager_find_view(&g_space_manager, vsm->spaces[i].sid);
        view_serialize(rsp, view, flags);
        if (i < buf_len(vsm->spaces) - 1) fprintf(rsp, ",");
    }
    fprintf(rsp, "]\n");

    return true;
}

static void virtual_space_install_signal_handlers(void)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    int signals[3] = { SIGTERM, SIGINT, SIGHUP };

    for (int i = 0; i < 3; ++i) {
        int sig = signals[i];
        signal(sig, SIG_IGN);

        dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, sig, 0, dispatch_get_main_queue());
        dispatch_source_set_event_handler(source, ^{
            event_loop_post(&g_event_loop, VIRTUAL_SPACE_EXIT, (void *)(intptr_t) sig, 0);
        });
        dispatch_resume(source);
        vsm->signal_source[i] = source;
    }
}

static void virtual_space_uninstall_signal_handlers(void)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    int signals[3] = { SIGTERM, SIGINT, SIGHUP };

    for (int i = 0; i < 3; ++i) {
        if (vsm->signal_source[i]) {
            dispatch_source_cancel(vsm->signal_source[i]);
            dispatch_release(vsm->signal_source[i]);
            vsm->signal_source[i] = NULL;
        }
        signal(signals[i], SIG_DFL);
    }
}

void virtual_space_restore_all_windows(void)
{
    if (!g_virtual_space_manager.enabled) return;

    uint32_t main_did = display_manager_main_display_id();
    SLSDisableUpdate(g_connection);

    table_for (struct window *window, g_window_manager.window, {
        if (window->vs_hidden) {
            uint32_t did = virtual_space_display(window->vsid);
            if (!did || !virtual_space_find_display(did)) did = main_did;
            virtual_space_unhide_window(window, did, true);
        }
    })

    SLSReenableUpdate(g_connection);
}

void virtual_space_handle_exit(int sig)
{
    virtual_space_restore_all_windows();
    fflush(stdout);
    fflush(stderr);

    signal(sig, SIG_DFL);
    raise(sig);
}

//
// NOTE: Windows left in a corner by a yabai that crashed (no chance to restore them) would stay
// there; bring any floating window that is all but off its display back to the middle.
//
static void virtual_space_recover_stranded_window(struct window *window)
{
    if (window_check_flag(window, WINDOW_MINIMIZE)) return;
    if (window_manager_find_managed_window(&g_window_manager, window)) return;

    CGRect frame = window_ax_frame(window);
    if (frame.size.width <= 0 || frame.size.height <= 0) return;

    uint32_t did = window_display_id_real(window->id);
    if (!did) return;

    CGRect bounds = CGDisplayBounds(did);
    CGRect overlap = CGRectIntersection(frame, bounds);
    if (!CGRectIsNull(overlap) && overlap.size.width > 2 && overlap.size.height > 2) return;

    CGPoint center = display_center(did);
    window_manager_set_window_frame_raw(window, center.x - frame.size.width / 2, center.y - frame.size.height / 2, frame.size.width, frame.size.height);
}

bool virtual_space_enable(int count)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    if (vsm->enabled) return true;

    int display_count = 0;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list || !display_count) return false;

    if (count < 1) count = VIRTUAL_SPACE_DEFAULT_COUNT;
    if (count < display_count) count = display_count;

    vsm->count = count;
    vsm->spaces = NULL;
    vsm->displays = NULL;
    vsm->next_sid = VIRTUAL_SPACE_SID_BASE + 1;
    vsm->suppress_pid = 0;

    uint32_t current_did = g_display_manager.current_display_id;
    if (!virtual_space_display_is_active(current_did, display_list, display_count)) current_did = display_list[0];

    for (int i = 0; i < count; ++i) {
        buf_push(vsm->spaces, ((struct virtual_space) { .sid = vsm->next_sid++, .did = current_did, .last_wid = 0 }));
    }

    bool sa = scripting_addition_is_loaded();

    //
    // NOTE: Map the native spaces onto virtual spaces by mission-control index, so windows keep the
    // number they had. Each display keeps one native "home" space: the first user space when the
    // scripting addition can destroy the rest, otherwise the one it shows now.
    //

    int native_count = 0;
    int native_capacity = 64;
    uint64_t *native_sid = malloc(sizeof(uint64_t) * native_capacity); // alloc: freed below; grows with the native space count
    uint64_t *native_vsid = malloc(sizeof(uint64_t) * native_capacity);
    uint32_t *native_did = malloc(sizeof(uint32_t) * native_capacity);
    uint64_t home_sid[display_count];
    uint64_t current_sid[display_count];
    int extra_count = 0;

    for (int i = 0; i < display_count; ++i) {
        uint32_t did = display_list[i];
        home_sid[i] = 0;
        current_sid[i] = display_space_id_real(did);

        int space_count = 0;
        uint64_t *space_list = display_space_list_real(did, &space_count);

        uint64_t first_user = 0;
        for (int j = 0; j < space_count; ++j) {
            if (!space_is_user(space_list[j])) continue;
            if (!first_user) first_user = space_list[j];
            else ++extra_count;

            if (native_count == native_capacity) {
                native_capacity *= 2;
                native_sid = realloc(native_sid, sizeof(uint64_t) * native_capacity);
                native_vsid = realloc(native_vsid, sizeof(uint64_t) * native_capacity);
                native_did = realloc(native_did, sizeof(uint32_t) * native_capacity);
            }

            int index = space_manager_mission_control_index(space_list[j]);
            native_sid[native_count] = space_list[j];
            native_did[native_count] = did;
            native_vsid[native_count] = (index >= 1 && index <= count) ? vsm->spaces[index-1].sid : 0;
            if (native_vsid[native_count]) vsm->spaces[index-1].did = did;
            ++native_count;
        }

        bool current_is_user = current_sid[i] && space_is_user(current_sid[i]);
        home_sid[i] = (sa || !current_is_user) ? first_user : current_sid[i];

        uint64_t shown_sid = current_is_user ? current_sid[i] : first_user;
        int shown_index = shown_sid ? space_manager_mission_control_index(shown_sid) : 0;
        uint64_t active = (shown_index >= 1 && shown_index <= count) ? vsm->spaces[shown_index-1].sid : 0;
        buf_push(vsm->displays, ((struct virtual_display) { .did = did, .sid = active, .home_sid = home_sid[i] ? home_sid[i] : current_sid[i] }));
    }

    for (int i = 0; i < buf_len(vsm->displays); ++i) {
        if (vsm->displays[i].sid) continue;

        for (int j = 0; j < buf_len(vsm->spaces); ++j) {
            bool taken = false;
            for (int k = 0; k < buf_len(vsm->displays); ++k) {
                if (vsm->displays[k].sid == vsm->spaces[j].sid) { taken = true; break; }
            }

            for (int k = 0; k < native_count && !taken; ++k) {
                if (native_vsid[k] == vsm->spaces[j].sid && native_did[k] != vsm->displays[i].did) taken = true;
            }

            if (!taken) {
                vsm->displays[i].sid = vsm->spaces[j].sid;
                vsm->spaces[j].did = vsm->displays[i].did;
                break;
            }
        }
    }

    vsm->enabled = true;

    float animation_duration = g_window_manager.window_animation_duration;
    g_window_manager.window_animation_duration = 0.0f;

    //
    // NOTE: The native views go quiet: their windows move to the virtual views below.
    //

    table_for (struct view *view, g_space_manager.view, {
        if (!virtual_space_is_virtual(view->sid) && view->layout != VIEW_FLOAT) {
            view_clear(view);
        }
    })

    for (int i = 0; i < buf_len(g_space_manager.labels); ++i) {
        struct space_label *label = &g_space_manager.labels[i];
        if (virtual_space_is_virtual(label->sid)) continue;

        int index = space_manager_mission_control_index(label->sid);
        if (index >= 1 && index <= count) label->sid = vsm->spaces[index-1].sid;
    }

    if (sa) {
        for (int i = 0; i < display_count; ++i) {
            if (home_sid[i] && current_sid[i] && home_sid[i] != current_sid[i] && space_is_user(current_sid[i])) {
                scripting_addition_focus_space(home_sid[i]);
            }
        }
    }

    for (int i = 0; i < display_count; ++i) {
        int move_count = 0;
        uint32_t *move_list = ts_alloc_list(uint32_t, g_window_manager.window.count + 1);

        table_for (struct window *window, g_window_manager.window, {
            if (window_check_flag(window, WINDOW_FULLSCREEN)) continue;

            uint64_t real_sid = window_space_real(window->id);
            if (!real_sid) continue;

            for (int j = 0; j < native_count; ++j) {
                if (native_sid[j] != real_sid || native_did[j] != display_list[i]) continue;

                window->vsid = native_vsid[j] ? native_vsid[j] : vsm->displays[i].sid;
                window->vs_hidden = false;
                if (home_sid[i] && real_sid != home_sid[i]) move_list[move_count++] = window->id;
                break;
            }
        })

        if (move_count && home_sid[i]) {
            space_manager_move_window_list_to_space_real(home_sid[i], move_list, move_count);
        }
    }

    SLSDisableUpdate(g_connection);

    for (int i = 0; i < buf_len(vsm->spaces); ++i) {
        uint64_t sid = vsm->spaces[i].sid;
        space_manager_find_view(&g_space_manager, sid);
        window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, sid);
    }

    table_for (struct window *window, g_window_manager.window, {
        if (!window->vsid) continue;

        if (virtual_space_is_visible(window->vsid)) {
            virtual_space_recover_stranded_window(window);
        } else {
            uint32_t did = virtual_space_display(window->vsid);
            if (did) virtual_space_hide_window(window, did);
        }
    })

    SLSReenableUpdate(g_connection);
    g_window_manager.window_animation_duration = animation_duration;

    //
    // NOTE: With virtual spaces on yabai never creates native spaces, and removes the extra ones
    // (a yabairc that ran `space --create` on every launch once left ~2000 behind). Fullscreen
    // spaces are never touched. Their windows were moved to the home space above.
    //

    if (sa) {
        for (int i = 0; i < display_count; ++i) {
            for (int tries = 0; tries < 30 && display_manager_display_is_animating(display_list[i]); ++tries) {
                usleep(100000);
            }
        }

        int destroyed = 0;
        for (int i = 0; i < native_count; ++i) {
            bool is_home = false;
            for (int j = 0; j < display_count; ++j) {
                if (home_sid[j] == native_sid[i]) { is_home = true; break; }
            }
            if (is_home) continue;

            if (scripting_addition_destroy_space(native_sid[i])) ++destroyed;
        }
        warn("yabai: virtual_spaces: destroyed %d extra native space(s)\n", destroyed);
    } else if (extra_count) {
        warn("yabai: virtual_spaces: %d extra native space(s) left in place (load the scripting addition to remove them); windows that stay on them are not part of any virtual space\n", extra_count);
    }

    free(native_sid);
    free(native_vsid);
    free(native_did);

    virtual_space_install_signal_handlers();

    g_space_manager.last_space_id = g_space_manager.current_space_id;
    g_space_manager.current_space_id = space_manager_active_space();
    event_signal_push(SIGNAL_SPACE_CHANGED, NULL);

    return true;
}

void virtual_space_disable(void)
{
    struct virtual_space_manager *vsm = &g_virtual_space_manager;
    if (!vsm->enabled) return;

    float animation_duration = g_window_manager.window_animation_duration;
    g_window_manager.window_animation_duration = 0.0f;

    virtual_space_restore_all_windows();

    for (int i = 0; i < buf_len(vsm->spaces); ++i) {
        uint64_t sid = vsm->spaces[i].sid;
        struct view *view = table_find(&g_space_manager.view, &sid);
        if (!view) continue;

        table_remove(&g_space_manager.view, &sid);
        view_clear(view);
        view_destroy(view);
        free(view);
    }

    int label_count = buf_len(g_space_manager.labels);
    int label_index[label_count > 0 ? label_count : 1];
    for (int i = 0; i < label_count; ++i) {
        label_index[i] = virtual_space_index(g_space_manager.labels[i].sid);
    }

    table_for (struct window *window, g_window_manager.window, {
        window->vsid = 0;
        window->vs_hidden = false;
    })

    buf_free(vsm->spaces);
    buf_free(vsm->displays);
    vsm->spaces = NULL;
    vsm->displays = NULL;
    vsm->enabled = false;

    //
    // NOTE: Labels follow the index back to the native space with the same mission-control index.
    //
    for (int i = 0, j = 0; i < label_count; ++i) {
        if (!virtual_space_is_virtual(g_space_manager.labels[j].sid)) { ++j; continue; }

        uint64_t native_sid = label_index[i] ? space_manager_mission_control_space(label_index[i]) : 0;
        if (native_sid && space_is_user(native_sid) && !space_manager_get_label_for_space(&g_space_manager, native_sid)) {
            g_space_manager.labels[j++].sid = native_sid;
        } else {
            free(g_space_manager.labels[j].label);
            vs_buf_remove(g_space_manager.labels, j);
        }
    }

    virtual_space_uninstall_signal_handlers();

    g_space_manager.current_space_id = space_manager_active_space();
    g_space_manager.last_space_id = g_space_manager.current_space_id;

    int display_count = 0;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    for (int i = 0; i < display_count; ++i) {
        uint64_t sid = display_space_id(display_list[i]);
        if (sid && space_is_user(sid)) {
            space_manager_mark_view_invalid(&g_space_manager, sid);
            struct view *view = space_manager_find_view(&g_space_manager, sid);
            if (view_is_invalid(view)) view_update(view);
            window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, sid);
        }
    }

    g_window_manager.window_animation_duration = animation_duration;
    event_signal_push(SIGNAL_SPACE_CHANGED, NULL);
}
