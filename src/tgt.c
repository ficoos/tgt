#include <stdio.h>

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <wait.h>

#define PROJECT_NAME "tgt"

#define TGT_ERROR g_tgt_error_quark()

#define DEFAULT_WIDTH 600
#define DEFAULT_HEIGHT 600

G_DEFINE_QUARK(g - tgt - error - quark, g_tgt_error)

typedef struct {
    float x;
    float y;
} vec2_t;

struct {
    gboolean pop;
    gboolean stdio;
    gint wrap_stdio;
    vec2_t location;
    vec2_t size;
} options = {.location = {-1, -1}, .size = {-1, -1}};

static vec2_t LOCATIONS[] = {
    {0, 1},
    {0.5, 1},
    {1, 1},
    {0, 0.5},
    {0.5, 0.5},
    {1, 0.5},
    {0, 0},
    {0.5, 0},
    {1, 0}};

typedef struct tiny_term {
    GtkWindow* window;
    VteTerminal* terminal;
    vec2_t location;
    vec2_t size;
    gchar** cmd;
    gboolean destroy_queued;
} tgt_term_t;

static gboolean parse_location_arg(
    const gchar* option_name,
    const gchar* value,
    gpointer data,
    GError** error);
static gboolean parse_size_arg(
    const gchar* option_name,
    const gchar* value,
    gpointer data,
    GError** error);

#pragma GCC diagnostic push
/* ignore to allow conversion of function pointer to void */
#pragma GCC diagnostic ignored "-Wpedantic"
static GOptionEntry entries[] = {
    {"pop",
     '\0',
     0,
     G_OPTION_ARG_NONE,
     &options.pop,
     "Should this be a pop up terminal",
     NULL},
    {"stdio",
     '\0',
     0,
     G_OPTION_ARG_NONE,
     &options.stdio,
     "Should stdin and stdout be perserved",
     NULL},
    {"location",
     '\0',
     0,
     G_OPTION_ARG_CALLBACK,
     parse_location_arg,
     ("Location on screen. Must be a number corrosponding to the numpad "
      "(e.g. 2: top-center, 6: middle-right)"),
     "LOCATION"},
    {"size",
     '\0',
     0,
     G_OPTION_ARG_CALLBACK,
     parse_size_arg,
     "width,height as fraction of the screen size (e.g. 0.5,0.5)",
     "WIDTH,HEIGHT"},
    {"wrap-stdio",
     '\0',
     G_OPTION_FLAG_HIDDEN,
     G_OPTION_ARG_INT,
     &options.wrap_stdio,
     "",
     "PID"},
    {NULL}};
#pragma GCC diagnostic pop

static gboolean parse_size_arg(
    const gchar* option_name G_GNUC_UNUSED,
    const gchar* value,
    gpointer data G_GNUC_UNUSED,
    GError** error G_GNUC_UNUSED)
{
    vec2_t size;
    gchar sentinal;
    if (sscanf(value, "%f,%f%c", &size.x, &size.y, &sentinal) != 2) {
        return FALSE;
    }
    if (size.x <= 0 || size.x > 1 || size.y <= 0 || size.y > 1) {
        return FALSE;
    }

    options.size = size;

    return TRUE;
}

static gboolean parse_location_arg(
    const gchar* option_name G_GNUC_UNUSED,
    const gchar* value,
    gpointer data G_GNUC_UNUSED,
    GError** error)
{
    gchar* endptr;
    gint64 result = g_ascii_strtoll(value, &endptr, 10);
    if (*endptr != '\0') {
        g_set_error(
            error,
            TGT_ERROR,
            EINVAL,
            "Error parsing option --location: Location must be a number");
        return FALSE;
    }
    if (result < 1 || result > 9) {
        g_set_error(
            error,
            TGT_ERROR,
            ERANGE,
            "Error parsing option --location: Location must be a number "
            "between 1 and 9");
        return FALSE;
    }

    options.location = LOCATIONS[result - 1];
    return TRUE;
}

static void on_spawn_callback(
    VteTerminal* terminal G_GNUC_UNUSED,
    GPid pid G_GNUC_UNUSED,
    GError* error,
    gpointer user_data)
{
    tgt_term_t* term = (tgt_term_t*)user_data;
    if (error) {
        gchar* cmd = g_strjoinv(" ", term->cmd);
        g_message("failed to exec cmd '%s': %s", cmd, error->message);
        g_error_free(error);
        g_free(cmd);
    }
}

static void on_child_exited(
    VteTerminal* vteterminal G_GNUC_UNUSED,
    gint status,
    gpointer user_data G_GNUC_UNUSED)
{
    exit(WEXITSTATUS(status));

static gboolean on_destroy_timeout(void *user_data) {
    tgt_term_t* term = (tgt_term_t*)user_data;
    if (term->destroy_queued) {
        gtk_widget_destroy(GTK_WIDGET(term->terminal));
    }

    return FALSE;
}

static gboolean on_focus_out(
    GtkWidget* widget G_GNUC_UNUSED,
    GdkEvent* event G_GNUC_UNUSED,
    gpointer user_data)
{
    tgt_term_t* term = (tgt_term_t*)user_data;
    term->destroy_queued = TRUE;
    g_timeout_add(10, on_destroy_timeout, term);

    return TRUE;
}

static gboolean on_focus_in(
    GtkWidget* widget G_GNUC_UNUSED,
    GdkEvent* event G_GNUC_UNUSED,
    gpointer user_data)
{
    tgt_term_t* term = (tgt_term_t*)user_data;
    term->destroy_queued = FALSE;

    return TRUE;
}

static void get_monitor_rect(const tgt_term_t* term, GdkRectangle* rect)
{
    GdkScreen* scr = gtk_window_get_screen(term->window);
    GdkDisplay* disp = gdk_screen_get_display(scr);
    gint wnd_x, wnd_y;
    gtk_window_get_position(term->window, &wnd_x, &wnd_y);
    GdkMonitor* mon = gdk_display_get_monitor_at_point(disp, wnd_x, wnd_y);
    gdk_monitor_get_geometry(mon, rect);
}

static void on_title_changed(VteTerminal* vteterm, gpointer user_data)
{
    tgt_term_t* term = (tgt_term_t*)user_data;
    const gchar* title = vte_terminal_get_window_title(vteterm);
    gtk_window_set_title(term->window, title);
}

static void init_window(tgt_term_t* term, GtkApplication* app)
{
    term->window = GTK_WINDOW(gtk_application_window_new(app));
    GtkSettings* default_settings = gtk_widget_get_settings(
        GTK_WIDGET(term->window));
    g_object_set(
        G_OBJECT(default_settings),
        "gtk-application-prefer-dark-theme",
        TRUE,
        NULL);
    gtk_window_set_title(term->window, "Terminal");
    if (options.pop) {
        gtk_window_set_skip_pager_hint(term->window, TRUE);
        gtk_window_set_skip_taskbar_hint(term->window, TRUE);
        gtk_window_set_modal(term->window, TRUE);
        gtk_window_set_decorated(term->window, FALSE);
        g_signal_connect(
            term->window,
            "focus-out-event",
            G_CALLBACK(on_focus_out),
            term);
        g_signal_connect(
            term->window,
            "focus-in-event",
            G_CALLBACK(on_focus_in),
            term);
    }
    GdkRectangle monitor_rect;
    get_monitor_rect(term, &monitor_rect);

    gtk_window_set_default_size(term->window, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    if (term->size.x > 0) {
        gtk_window_resize(
            term->window,
            term->size.x * monitor_rect.width,
            term->size.y * monitor_rect.height);
    } else {
        gtk_window_resize(term->window, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    }

    if (term->location.x > 0) {
        gint wnd_width, wnd_height;
        gtk_window_get_size(term->window, &wnd_width, &wnd_height);
        gtk_window_move(
            term->window,
            (monitor_rect.width - wnd_width) * term->location.x,
            (monitor_rect.height - wnd_height) * term->location.y);
    }
}

static GSettings* get_default_profile(void)
{
    GSettings* profiles_list = g_settings_new(
        "org.gnome.Terminal.ProfilesList");
    gchararray profile_id = g_settings_get_string(profiles_list, "default");
    gchararray path = g_strdup_printf(
        "/org/gnome/terminal/legacy/profiles:/:%s/",
        profile_id);
    GSettings* profile = g_settings_new_with_path(
        "org.gnome.Terminal.Legacy.Profile",
        path);
    g_free(path);
    g_free(profile_id);
    g_object_unref(profiles_list);

    return profile;
}

static void apply_profile(tgt_term_t* term)
{
    GSettings* profile = get_default_profile();
    // font
    {
        if (!g_settings_get_boolean(profile, "use-system-font")) {
            gchararray font_name = g_settings_get_string(profile, "font");
            PangoFontDescription* font = pango_font_description_from_string(
                font_name);
            g_free(font_name);
            vte_terminal_set_font(term->terminal, font);
            // g_object_unref(font);
        }
    }
    // colors
    {
        GdkRGBA background_color;
        GdkRGBA foreground_color;
        GdkRGBA palette[256] = {0};
        gchararray str = g_settings_get_string(profile, "background-color");
        gdk_rgba_parse(&background_color, str);
        g_free(str);
        str = g_settings_get_string(profile, "foreground-color");
        gdk_rgba_parse(&foreground_color, str);
        g_free(str);
        gchararray* palette_strv = g_settings_get_strv(profile, "palette");
        int palette_size;
        for (palette_size = 0; palette_strv[palette_size] != NULL;
             palette_size++) {
            gdk_rgba_parse(&palette[palette_size], palette_strv[palette_size]);
        }
        g_strfreev(palette_strv);
        vte_terminal_set_colors(
            term->terminal,
            &foreground_color,
            &background_color,
            palette,
            palette_size);
    }
    // title
    {
        gchar* title = g_settings_get_string(profile, "title");
        gtk_window_set_title(term->window, title);
        g_free(title);
        if (g_settings_get_boolean(
                profile,
                "show-foreground-process-in-title")) {

            g_signal_connect(
                term->terminal,
                "window-title-changed",
                G_CALLBACK(on_title_changed),
                term);
        }
    }
    // cursor blink mode
    {
        gchar* blink_mode_str = g_settings_get_string(profile, "cursor-blink-mode");
        VteCursorBlinkMode blink_mode = VTE_CURSOR_BLINK_SYSTEM;
        gtk_window_set_title(term->window, blink_mode_str);
        if (strcmp(blink_mode_str, "system") == 0) {
            blink_mode = VTE_CURSOR_BLINK_SYSTEM;
        } else if (strcmp(blink_mode_str, "on") == 0) {
            blink_mode = VTE_CURSOR_BLINK_ON;
        } else if (strcmp(blink_mode_str, "off") == 0) {
            blink_mode = VTE_CURSOR_BLINK_OFF;
        }

        vte_terminal_set_cursor_blink_mode(term->terminal, blink_mode);
        g_free(blink_mode_str);
    }
    vte_terminal_set_enable_bidi(
        term->terminal,
        g_settings_get_boolean(profile, "enable-bidi"));
    vte_terminal_set_bold_is_bright(
        term->terminal,
        g_settings_get_boolean(profile, "bold-is-bright"));
    vte_terminal_set_audible_bell(
        term->terminal,
        g_settings_get_boolean(profile, "audible-bell"));
    vte_terminal_set_scroll_on_output(
        term->terminal,
        g_settings_get_boolean(profile, "scroll-on-output"));

    g_object_unref(profile);
}

static void init_vte(tgt_term_t* term)
{
    term->terminal = VTE_TERMINAL(vte_terminal_new());
    gtk_container_add(GTK_CONTAINER(term->window), GTK_WIDGET(term->terminal));
    gchar* cmd = g_strjoinv(" ", term->cmd);
    g_free(cmd);

    apply_profile(term);

    g_signal_connect(
        term->terminal,
        "child-exited",
        G_CALLBACK(on_child_exited),
        &term);
}

static void activate(GtkApplication* app, gpointer user_data)
{
    tgt_term_t* term = (tgt_term_t*)user_data;
    init_window(term, app);
    init_vte(term);

    vte_terminal_spawn_async(
        VTE_TERMINAL(term->terminal),
        VTE_PTY_DEFAULT,
        NULL,
        term->cmd,
        NULL,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        NULL,
        -1,
        NULL,
        on_spawn_callback,
        term);

    gtk_widget_show_all(GTK_WIDGET(term->window));
}

static void wrap_stdio(void)
{
    gchar* stdin_name = g_strdup_printf("/proc/%d/fd/0", options.wrap_stdio);
    int stdin_fd = open(stdin_name, O_RDONLY);
    g_free(stdin_name);
    dup2(stdin_fd, 0);
    close(stdin_fd);
    gchar* stdout_name = g_strdup_printf("/proc/%d/fd/1", options.wrap_stdio);
    int stdout_fd = open(stdout_name, O_WRONLY);
    g_free(stdout_name);
    dup2(stdout_fd, 1);
    close(stdout_fd);
}

static inline const char* get_default_shell()
{
    const char* shell_env = getenv("SHELL");
    return shell_env ? shell_env : "sh";
}

static gint on_command_line(
    GApplication* application,
    GApplicationCommandLine* command_line,
    gpointer user_data)
{
    tgt_term_t* term = (tgt_term_t*)user_data;
    gint argc;
    gchar** argv = g_application_command_line_get_arguments(
        command_line,
        &argc);

    gchar** cmd = &argv[1];
    argc--;
    if (cmd[0] && strcmp(cmd[0], "--") == 0) {
        cmd++;
        argc--;
    }

    term->location = options.location;
    term->size = options.size;
    if (options.stdio) {
        gchar** t = g_malloc(sizeof(gchar*) * (argc + 5));
        int i = 0;
        t[i++] = g_strdup(argv[0]);
        t[i++] = g_strdup("--wrap-stdio");
        t[i++] = g_strdup_printf("%d", getpid());
        t[i++] = g_strdup("--");
        for (gchar** arg = cmd; *arg != NULL; arg++) {
            t[i++] = g_strdup(*arg);
        }
        t[i] = NULL;
        term->cmd = t;
    } else if (options.wrap_stdio) {
        wrap_stdio();
        execvp(cmd[0], cmd);
        exit(errno);
    } else if (argc == 0) {
        term->cmd = g_strdupv((char*[]){(char*)get_default_shell(), NULL});
    } else {
        term->cmd = g_strdupv(cmd);
    }

    g_strfreev(argv);
    activate(GTK_APPLICATION(application), term);

    return 0;
}

int main(int argc, char** argv)
{
    GtkApplication* app;
    int status;
    tgt_term_t term = {0};

    app = gtk_application_new(NULL, G_APPLICATION_HANDLES_COMMAND_LINE);
    g_application_set_option_context_parameter_string(
        G_APPLICATION(app),
        "[-- COMMAND…]");
    g_application_add_main_option_entries(G_APPLICATION(app), entries);
    g_signal_connect(app, "command-line", G_CALLBACK(on_command_line), &term);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}
