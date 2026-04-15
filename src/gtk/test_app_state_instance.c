#include "app_state.h"

#include <glib.h>

static gboolean
instance_id_char_allowed(char c)
{
    return g_ascii_isalnum(c) || c == '-' || c == '_' || c == '.';
}

static void
assert_instance_id_valid(const char *instance_id)
{
    g_assert_nonnull(instance_id);
    g_assert_true(instance_id[0] != '\0');
    for (const char *p = instance_id; *p; p++)
        g_assert_true(instance_id_char_allowed(*p));
}

static void
reset_instance_id(void)
{
    app_state()->instance_id[0] = '\0';
}

static void
clear_instance_env(void)
{
    g_unsetenv("PRETTYMUX");
    g_unsetenv("PRETTYMUX_SOCKET");
    g_unsetenv("PRETTYMUX_INSTANCE_ID");
}

static void
test_default_instance_id_is_stable(void)
{
    const char *first;
    const char *second;

    reset_instance_id();
    first = app_state_get_instance_id();
    second = app_state_get_instance_id();

    assert_instance_id_valid(first);
    g_assert_cmpstr(first, ==, second);
}

static void
test_init_instance_id_from_env(void)
{
    reset_instance_id();
    g_unsetenv("PRETTYMUX");
    g_setenv("PRETTYMUX_INSTANCE_ID", "phase6!target@A-_.9", TRUE);
    app_state_init_instance_id_from_env();
    g_assert_cmpstr(app_state_get_instance_id(), ==, "phase6targetA-_.9");
    clear_instance_env();
}

static void
test_set_instance_id_sanitizes_value(void)
{
    app_state_set_instance_id("phase6!target@A-_.9");
    g_assert_cmpstr(app_state_get_instance_id(), ==, "phase6targetA-_.9");
}

static void
test_set_instance_id_invalid_value_falls_back(void)
{
    app_state_set_instance_id("!!!");
    assert_instance_id_valid(app_state_get_instance_id());
}

static void
test_init_instance_id_from_inside_prettymux_uses_child_id(void)
{
    reset_instance_id();
    g_setenv("PRETTYMUX", "1", TRUE);
    g_setenv("PRETTYMUX_SOCKET", "/tmp/prettymux-phase6-parent.sock", TRUE);
    g_setenv("PRETTYMUX_INSTANCE_ID", "phase6-parent", TRUE);
    app_state_init_instance_id_from_env();
    g_assert_cmpstr(app_state_get_instance_id(), ==, "phase6-parent-child");
    clear_instance_env();
}

static void
test_init_instance_id_from_inside_prettymux_sanitizes_parent(void)
{
    reset_instance_id();
    g_setenv("PRETTYMUX", "1", TRUE);
    g_setenv("PRETTYMUX_SOCKET", "/tmp/prettymux-phase6-parent.sock", TRUE);
    g_setenv("PRETTYMUX_INSTANCE_ID", "phase6!parent@", TRUE);
    app_state_init_instance_id_from_env();
    g_assert_cmpstr(app_state_get_instance_id(), ==, "phase6parent-child");
    clear_instance_env();
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/app-state/instance/default-stable",
                    test_default_instance_id_is_stable);
    g_test_add_func("/app-state/instance/init-from-env",
                    test_init_instance_id_from_env);
    g_test_add_func("/app-state/instance/sanitize",
                    test_set_instance_id_sanitizes_value);
    g_test_add_func("/app-state/instance/invalid-fallback",
                    test_set_instance_id_invalid_value_falls_back);
    g_test_add_func("/app-state/instance/init-from-inside-prettymux-child",
                    test_init_instance_id_from_inside_prettymux_uses_child_id);
    g_test_add_func("/app-state/instance/init-from-inside-prettymux-sanitize",
                    test_init_instance_id_from_inside_prettymux_sanitizes_parent);

    return g_test_run();
}
