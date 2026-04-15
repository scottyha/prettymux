#include "app_state.h"

#include <glib.h>
#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#define PRETTYMUX_DEFAULT_INSTANCE_ID "default"

static gboolean
instance_id_char_allowed(char c)
{
    return g_ascii_isalnum(c) || c == '-' || c == '_' || c == '.';
}

static gboolean
instance_id_is_valid(const char *instance_id)
{
    if (!instance_id || !instance_id[0])
        return FALSE;

    for (const char *p = instance_id; *p; p++) {
        if (!instance_id_char_allowed(*p))
            return FALSE;
    }

    return TRUE;
}

static void
sanitize_instance_id(const char *input, char *out, gsize out_len)
{
    gsize out_idx = 0;

    if (!out || out_len == 0)
        return;

    out[0] = '\0';
    if (!input || !input[0])
        return;

    for (const char *p = input; *p && out_idx + 1 < out_len; p++) {
        if (instance_id_char_allowed(*p))
            out[out_idx++] = *p;
    }
    out[out_idx] = '\0';
}

static void
assign_instance_id(AppState *state, const char *preferred_id)
{
    char base_id[sizeof(state->instance_id)] = {0};

    sanitize_instance_id(preferred_id, base_id, sizeof(base_id));
    if (!base_id[0])
        g_strlcpy(base_id, PRETTYMUX_DEFAULT_INSTANCE_ID, sizeof(base_id));
    g_strlcpy(state->instance_id, base_id, sizeof(state->instance_id));
}

static void
assign_default_instance_id(AppState *state)
{
    assign_instance_id(state, PRETTYMUX_DEFAULT_INSTANCE_ID);
}

AppState *
app_state(void)
{
    static AppState state = {
        .ghostty_default_font_size = 0.0f,
        .main_window_active = FALSE,
        .terminal_search_total = -1,
        .terminal_search_selected = -1,
    };

    return &state;
}

const char *
app_state_get_instance_id(void)
{
    AppState *state = app_state();

    if (!state->instance_id[0])
        assign_default_instance_id(state);
    return state->instance_id;
}

void
app_state_set_instance_id(const char *instance_id)
{
    AppState *state = app_state();
    assign_instance_id(state, instance_id);
}

#ifndef G_OS_WIN32
static gboolean
socket_path_is_connectable(const char *path)
{
    int fd;
    struct sockaddr_un addr = {0};
    gboolean ok = FALSE;

    if (!path || !path[0])
        return FALSE;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return FALSE;

    addr.sun_family = AF_UNIX;
    g_strlcpy(addr.sun_path, path, sizeof(addr.sun_path));
    ok = connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    close(fd);
    return ok;
}

static gboolean
parse_instance_id_from_socket_name(const char *name, char *out, gsize out_len)
{
    const char *prefix = "prettymux-";
    const char *suffix = ".sock";
    gsize len;
    gsize prefix_len;
    gsize suffix_len;
    gsize id_len;

    if (!name || !out || out_len == 0)
        return FALSE;

    out[0] = '\0';
    len = strlen(name);
    prefix_len = strlen(prefix);
    suffix_len = strlen(suffix);
    if (len <= prefix_len + suffix_len)
        return FALSE;
    if (!g_str_has_prefix(name, prefix) || !g_str_has_suffix(name, suffix))
        return FALSE;

    id_len = len - prefix_len - suffix_len;
    if (id_len + 1 > out_len)
        return FALSE;

    memcpy(out, name + prefix_len, id_len);
    out[id_len] = '\0';
    return instance_id_is_valid(out);
}

static gboolean
parse_numeric_instance_id(const char *instance_id, unsigned long long *out_value)
{
    char *end = NULL;
    unsigned long long value;

    if (!instance_id || !instance_id[0])
        return FALSE;

    errno = 0;
    value = g_ascii_strtoull(instance_id, &end, 10);
    if (errno != 0 || !end || *end != '\0')
        return FALSE;

    if (out_value)
        *out_value = value;
    return TRUE;
}

static gint
compare_instance_id(const char *a, const char *b)
{
    unsigned long long a_num = 0;
    unsigned long long b_num = 0;
    gboolean a_is_numeric = parse_numeric_instance_id(a, &a_num);
    gboolean b_is_numeric = parse_numeric_instance_id(b, &b_num);

    if (a_is_numeric && b_is_numeric) {
        if (a_num < b_num)
            return -1;
        if (a_num > b_num)
            return 1;
    } else if (a_is_numeric != b_is_numeric) {
        return a_is_numeric ? -1 : 1;
    }

    return g_strcmp0(a, b);
}

static gint
compare_instance_id_ptr(gconstpointer a, gconstpointer b)
{
    const char *ia = *(const char *const *)a;
    const char *ib = *(const char *const *)b;
    return compare_instance_id(ia, ib);
}
#endif

void
app_state_init_instance_id_from_env(void)
{
    const char *env_instance_id = g_getenv("PRETTYMUX_INSTANCE_ID");
    const char *inside_prettymux = g_getenv("PRETTYMUX");
    const char *parent_socket = g_getenv("PRETTYMUX_SOCKET");

    if (!env_instance_id || !env_instance_id[0])
        return;

    if (inside_prettymux && inside_prettymux[0] &&
        parent_socket && parent_socket[0]) {
        char parent_id[sizeof(app_state()->instance_id)] = {0};
        char nested_id[sizeof(app_state()->instance_id)] = {0};

        sanitize_instance_id(env_instance_id, parent_id, sizeof(parent_id));
        if (!parent_id[0])
            g_strlcpy(parent_id, PRETTYMUX_DEFAULT_INSTANCE_ID,
                      sizeof(parent_id));

        g_snprintf(nested_id, sizeof(nested_id), "%s-child", parent_id);
        app_state_set_instance_id(nested_id);
        return;
    }

    app_state_set_instance_id(env_instance_id);
}

GPtrArray *
app_state_list_instances(void)
{
    GPtrArray *instances = g_ptr_array_new_with_free_func(g_free);

#ifdef G_OS_WIN32
    g_ptr_array_add(instances, g_strdup(app_state_get_instance_id()));
    return instances;
#else
    g_autoptr(GHashTable) seen = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                       g_free, NULL);
    DIR *dir = opendir("/tmp");
    struct dirent *ent;

    if (!dir)
        return instances;

    while ((ent = readdir(dir)) != NULL) {
        char instance_id[128];
        char socket_path[256];
        size_t name_len = strlen(ent->d_name);

        if (!parse_instance_id_from_socket_name(ent->d_name, instance_id,
                                                sizeof(instance_id)))
            continue;
        if (g_hash_table_contains(seen, instance_id))
            continue;
        if (name_len + 6 > sizeof(socket_path))
            continue;

        memcpy(socket_path, "/tmp/", 5);
        memcpy(socket_path + 5, ent->d_name, name_len + 1);
        if (!socket_path_is_connectable(socket_path))
            continue;

        g_hash_table_add(seen, g_strdup(instance_id));
        g_ptr_array_add(instances, g_strdup(instance_id));
    }

    closedir(dir);

    if (instances->len > 1)
        g_ptr_array_sort(instances, compare_instance_id_ptr);

    return instances;
#endif
}
