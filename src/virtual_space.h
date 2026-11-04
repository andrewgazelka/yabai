#ifndef VIRTUAL_SPACE_H
#define VIRTUAL_SPACE_H

//
// Virtual spaces (`yabai -m config virtual_spaces on`), AeroSpace style.
//
// Each display keeps one real macOS space. yabai owns N virtual spaces with synthetic space ids
// (VIRTUAL_SPACE_SID_BASE + n); each display shows one of them at a time. Every tracked window
// carries the virtual space it belongs to (window->vsid). Switching moves the windows of the old
// virtual space into a corner of the display (leaving a 1px sliver, because macOS clamps windows
// that are moved fully off-screen) and brings the windows of the new one back, by re-tiling its
// view or restoring the saved frames of floating windows. No native space switching, no
// scripting addition.
//
// The space helpers (display_space_id, display_space_list, window_space, space_display_id,
// space_is_visible, space_window_list, ...) answer with virtual space ids while this is on, so
// the rest of yabai (views, queries, selectors, signals) works on virtual spaces unchanged.
//

#define VIRTUAL_SPACE_SID_BASE      0x7a000000ULL
#define VIRTUAL_SPACE_SID_LIMIT     0x7b000000ULL
#define VIRTUAL_SPACE_DEFAULT_COUNT 10

struct virtual_space
{
    uint64_t sid;
    uint32_t did;
    uint32_t last_wid;
};

struct virtual_display
{
    uint32_t did;
    uint64_t sid;
};

struct virtual_space_manager
{
    bool enabled;
    int count;
    uint64_t next_sid;
    struct virtual_space *spaces;
    struct virtual_display *displays;
    pid_t suppress_pid;
    uint64_t suppress_until;
    dispatch_source_t signal_source[3];
};

extern struct virtual_space_manager g_virtual_space_manager;

static inline bool virtual_space_is_virtual(uint64_t sid)
{
    return sid >= VIRTUAL_SPACE_SID_BASE && sid < VIRTUAL_SPACE_SID_LIMIT;
}

bool virtual_space_enabled(void);
struct virtual_space *virtual_space_find(uint64_t sid);
int virtual_space_index(uint64_t sid);
uint64_t virtual_space_at_index(int index);
uint64_t virtual_space_prev(uint64_t sid);
uint64_t virtual_space_next(uint64_t sid);
uint64_t virtual_space_first(void);
uint64_t virtual_space_last(void);
uint64_t virtual_space_translate(uint64_t sid);
uint32_t virtual_space_display(uint64_t sid);
bool virtual_space_is_visible(uint64_t sid);
uint64_t virtual_space_active_for_display(uint32_t did);
uint64_t virtual_space_display_space(uint32_t did, uint64_t real_sid);
uint64_t *virtual_space_list_for_display(uint32_t did, int *count);
uint64_t virtual_space_window_space(uint32_t wid);
uint32_t *virtual_space_window_list(uint64_t *space_list, int space_count, int cid, int *count, bool include_minimized);
bool virtual_space_window_is_hidden(struct window *window);
void virtual_space_save_requested_frame(struct window *window, float x, float y, float w, float h);

enum space_op_error virtual_space_focus(uint64_t sid);
enum space_op_error virtual_space_focus_on_display(uint64_t sid, uint32_t did);
enum space_op_error virtual_space_create(uint32_t did);
enum space_op_error virtual_space_destroy(uint64_t sid);
void virtual_space_move_window(struct window *window, uint64_t sid);
void virtual_space_window_did_receive_focus(struct window *window);
bool virtual_space_should_ignore_focus(struct window *window);
void virtual_space_suppress_finder_focus(void);
void virtual_space_window_did_move(struct window *window);
void virtual_space_window_did_deminimize(struct window *window);
void virtual_space_application_did_unhide(struct application *application);
void virtual_space_displays_changed(void);
void virtual_space_rehide_all(void);
void virtual_space_serialize_display_spaces(FILE *rsp, uint32_t did);
bool virtual_space_query_spaces(FILE *rsp, uint64_t flags);

bool virtual_space_enable(int count);
void virtual_space_disable(void);
void virtual_space_restore_all_windows(void);
void virtual_space_handle_exit(int sig);

#endif
