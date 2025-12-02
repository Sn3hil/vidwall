#include <gtk/gtk.h>
#include <gtk4-layer-shell.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <iostream>
#include <locale.h>
#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include "hyprland_ipc.h"
#include "cli_args.h"

static CliArgs g_args;


class HyprVidWall;
static void on_focus_changed(bool has_focus, HyprVidWall *self);

struct FocusChangeData {
    class HyprVidWall *self;
    bool has_focus;
    guint *pending_id;  // Pointer to the pending callback ID
};

class HyprVidWall {
private:
    GtkApplication *app;
    GtkWindow *window;
    GtkWidget *gl_area;
    mpv_handle *mpv;
    mpv_render_context *mpv_gl;
    guint render_timer_id;
    guint event_timer_id;
    HyprlandIPC hypr_ipc;
    bool is_paused;
    CliArgs args;
    guint pending_resize_id;
    int64_t last_video_width = 0;
    int64_t last_video_height = 0;
    
    static void *get_proc_address(void *ctx, const char *name) {
        (void)ctx;
        return (void *)eglGetProcAddress(name);
    }
    
    static void on_mpv_render_update(void *ctx) {
        auto *self = static_cast<HyprVidWall*>(ctx);
        gtk_gl_area_queue_render(GTK_GL_AREA(self->gl_area));
    }
    
    // 60 FPS render timer
    static gboolean on_render_timer(gpointer user_data) {
        auto *self = static_cast<HyprVidWall*>(user_data);
        gtk_gl_area_queue_render(GTK_GL_AREA(self->gl_area));
        return G_SOURCE_CONTINUE;
    }


    
    //Event Handler
    static gboolean on_event_timer(gpointer user_data) {
        auto *self = static_cast<HyprVidWall*>(user_data);
        self->handle_mpv_events();
        return G_SOURCE_CONTINUE;
    }
    
    void handle_mpv_events() {
        while (mpv) {
            mpv_event *event = mpv_wait_event(mpv, 0);
            if (event->event_id == MPV_EVENT_NONE) break;
            
            if (event->event_id == MPV_EVENT_END_FILE) {
                mpv_event_end_file *ef = (mpv_event_end_file *)event->data;
                if (ef->reason == MPV_END_FILE_REASON_ERROR) {
                    std::cerr << "Error, reloading..." << std::endl;
                    load_video();
                }
            } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
                std::cout << "Video loaded" << std::endl;
                
                // Get video dimensions and adjust window
                int64_t width = 0, height = 0;
                mpv_get_property(mpv, "width", MPV_FORMAT_INT64, &width);
                mpv_get_property(mpv, "height", MPV_FORMAT_INT64, &height);
                
                // Only resize if dimensions changed
                if (width > 0 && height > 0 && 
                    (width != last_video_width || height != last_video_height)) {
                    last_video_width = width;
                    last_video_height = height;
                    adjust_window_for_aspect_ratio(width, height);
                }
            }
        }
    }
    
    void adjust_window_for_aspect_ratio(int64_t video_width, int64_t video_height) {
        double aspect_ratio = (double)video_width / (double)video_height;
        
        std::cout << "Video: " << video_width << "x" << video_height 
                  << " (aspect: " << aspect_ratio << ")" << std::endl;
        
        // Get screen dimensions
        GdkDisplay *display = gdk_display_get_default();
        GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
        GdkMonitor *monitor = gdk_display_get_monitor_at_surface(display, surface);
        GdkRectangle geom;
        gdk_monitor_get_geometry(monitor, &geom);
        
        // Cancel any pending resize
        if (pending_resize_id > 0) {
            g_source_remove(pending_resize_id);
            pending_resize_id = 0;
        }
        
        // Vertical video (9:16 or similar)
        if (aspect_ratio < 1.0) {
            // Calculate width to maintain aspect ratio at full height
            int window_width = (int)(geom.height * aspect_ratio);
            
            // Schedule resize on GTK main thread
            struct ResizeData {
                HyprVidWall *self;
                int width;
                int height;
            };
            
            auto *data = new ResizeData{this, window_width, geom.height};
            
            pending_resize_id = g_idle_add([](gpointer user_data) -> gboolean {
                auto *resize_data = static_cast<ResizeData*>(user_data);
                
                // Clear the pending ID first
                resize_data->self->pending_resize_id = 0;
                
                // Reset anchors and set to top/bottom only
                gtk_layer_set_anchor(resize_data->self->window, GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
                gtk_layer_set_anchor(resize_data->self->window, GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
                gtk_layer_set_anchor(resize_data->self->window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
                gtk_layer_set_anchor(resize_data->self->window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
                
                // Set explicit width and height
                gtk_widget_set_size_request(GTK_WIDGET(resize_data->self->window), 
                                           resize_data->width, 
                                           resize_data->height);
                
                delete resize_data;
                return G_SOURCE_REMOVE;
            }, data);
            
            std::cout << "Vertical video: will resize to " << window_width << "x" << geom.height << "px" << std::endl;
        }
        // Horizontal video - keep fullscreen
        else {
            std::cout << "Horizontal video: keeping fullscreen" << std::endl;
        }
    }
    
    static void on_activate(GtkApplication *app, gpointer user_data) {
        auto *self = static_cast<HyprVidWall*>(user_data);
        (void)app;
        self->setup_window();
        self->setup_mpv();
        self->setup_gl_rendering();
        
       
        self->render_timer_id = g_timeout_add(16, on_render_timer, self);

        self->event_timer_id = g_timeout_add(250, on_event_timer, self);
        
        // Only start IPC if auto-pause is enabled
        if (self->args.auto_pause) {
            if (self->hypr_ipc.connect()) {
                self->hypr_ipc.start_listening([self](bool has_focus) {
                    on_focus_changed(has_focus, self);
                });
                std::cout << "Auto-pause enabled" << std::endl;
            } else {
                std::cout << "Hyprland IPC not available - auto-pause disabled" << std::endl;
            }
        } else {
            std::cout << "Auto-pause disabled" << std::endl;
        }
        
        self->load_video();
    }

public:
    guint pending_focus_change_id; 
    
    void pause_video() {
        if (!mpv || is_paused) return;
        const char *cmd[] = {"set", "pause", "yes", nullptr};
        mpv_command_async(mpv, 0, cmd);
        is_paused = true;
        std::cout << "Video paused (window focused)" << std::endl;
    }

    void resume_video() {
        if (!mpv || !is_paused) return;
        const char *cmd[] = {"set", "pause", "no", nullptr};
        mpv_command_async(mpv, 0, cmd);
        is_paused = false;
        std::cout << "Video resumed (workspace empty)" << std::endl;
    }

private:
    void setup_window() {
        window = GTK_WINDOW(gtk_application_window_new(app));
        gtk_window_set_title(window, "vidwall");
        
        gtk_layer_init_for_window(window);
        gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_BACKGROUND);
        gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_exclusive_zone(window, -1);
        
        gl_area = gtk_gl_area_new();
        gtk_gl_area_set_required_version(GTK_GL_AREA(gl_area), 2, 1);  
        gtk_gl_area_set_auto_render(GTK_GL_AREA(gl_area), FALSE);
        
        g_signal_connect(gl_area, "realize", G_CALLBACK(on_gl_realize), this);
        g_signal_connect(gl_area, "render", G_CALLBACK(on_gl_render), this);
        g_signal_connect(gl_area, "unrealize", G_CALLBACK(on_gl_unrealize), this);
        
        gtk_window_set_child(window, gl_area);
        
        gtk_window_present(window);
        
        std::cout << "Window ready" << std::endl;
    }
    
    void setup_mpv() {
        mpv = mpv_create();
        if (!mpv) {
            std::cerr << "Failed to create mpv" << std::endl;
            return;
        }
        
        // Minimal settings for performance
        mpv_set_option_string(mpv, "vo", "libmpv");
        mpv_set_option_string(mpv, "hwdec", args.no_hwdec ? "no" : "auto");
        
        //CLI args
        mpv_set_option_string(mpv, "loop-file", args.loop ? "inf" : "no");
        mpv_set_option_string(mpv, "audio", args.mute ? "no" : "yes");
        if (!args.mute) {
            mpv_set_option_string(mpv, "volume", "50");
        }
        
        mpv_set_option_string(mpv, "video-sync", "display-vdrop");
        mpv_set_option_string(mpv, "opengl-swapinterval", "0");
        mpv_set_option_string(mpv, "scale", "bilinear");
        mpv_set_option_string(mpv, "dscale", "bilinear");
        mpv_set_option_string(mpv, "cscale", "bilinear");
        mpv_set_option_string(mpv, "vd-lavc-dr", "yes");
        mpv_set_option_string(mpv, "vd-lavc-threads", "0");
        mpv_set_option_string(mpv, "background", "none"); 
        
        // Downscale 4K to 1080p unless disabled
        if (!args.no_downscale) {
            mpv_set_option_string(mpv, "vf", "scale=w=1920:h=-1");
        }
        
        if (mpv_initialize(mpv) < 0) {
            std::cerr << "Failed to initialize mpv" << std::endl;
            return;
        }
        
        std::cout << "MPV ready" << std::endl;
        if (!args.mute) std::cout << "  Audio: enabled (50% volume)" << std::endl;
        if (args.loop) std::cout << "  Loop: enabled" << std::endl;
    }
    
    void setup_gl_rendering() {
        gtk_gl_area_make_current(GTK_GL_AREA(gl_area));
        
        if (gtk_gl_area_get_error(GTK_GL_AREA(gl_area)) != nullptr) {
            std::cerr << "GL error" << std::endl;
            return;
        }
        
        mpv_opengl_init_params gl_init_params{
            .get_proc_address = get_proc_address,
            .get_proc_address_ctx = nullptr
        };
        
        mpv_render_param params[]{
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        
        if (mpv_render_context_create(&mpv_gl, mpv, params) < 0) {
            std::cerr << "Render context failed" << std::endl;
            return;
        }
        
        mpv_render_context_set_update_callback(mpv_gl, on_mpv_render_update, this);
        
        std::cout << "GL rendering ready (60 FPS)" << std::endl;
    }
    
    void load_video() {
        if (!mpv) return;
        const char *cmd[] = {"loadfile", args.video_path.c_str(), nullptr};
        mpv_command_async(mpv, 0, cmd);
        std::cout << "Loading: " << args.video_path << std::endl;
    }
    
    static void on_gl_realize(GtkGLArea *area, gpointer user_data) {
        (void)area; (void)user_data;
    }
    
    static gboolean on_gl_render(GtkGLArea *area, GdkGLContext *context, gpointer user_data) {
        auto *self = static_cast<HyprVidWall*>(user_data);
        (void)context;
        
        if (!self->mpv_gl) return FALSE;
        
       
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        int width = gtk_widget_get_width(GTK_WIDGET(area));
        int height = gtk_widget_get_height(GTK_WIDGET(area));
        
        int fbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        
        mpv_opengl_fbo mpv_fbo{
            .fbo = fbo,
            .w = width,
            .h = height,
            .internal_format = 0
        };
        
        int flip_y = 1;
        int block = 0;  
        
        mpv_render_param render_params[]{
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        
        mpv_render_context_render(self->mpv_gl, render_params);
        
        return TRUE;
    }
    
    static void on_gl_unrealize(GtkGLArea *area, gpointer user_data) {
        (void)area;
        auto *self = static_cast<HyprVidWall*>(user_data);
        
        if (self->render_timer_id > 0) {
            g_source_remove(self->render_timer_id);
            self->render_timer_id = 0;
        }
        if (self->event_timer_id > 0) {
            g_source_remove(self->event_timer_id);
            self->event_timer_id = 0;
        }
        
        gtk_gl_area_make_current(GTK_GL_AREA(self->gl_area));
        
        if (self->mpv_gl) {
            mpv_render_context_free(self->mpv_gl);
            self->mpv_gl = nullptr;
        }
    }

public:
    HyprVidWall(const CliArgs& cli_args) 
        : mpv_gl(nullptr), render_timer_id(0), event_timer_id(0), 
          is_paused(false), args(cli_args), pending_focus_change_id(0), pending_resize_id(0) {
        app = gtk_application_new("com.hyprvidwall.app", G_APPLICATION_NON_UNIQUE);
        g_signal_connect(app, "activate", G_CALLBACK(on_activate), this);
    }
    
    ~HyprVidWall() {
        if (mpv) mpv_terminate_destroy(mpv);
        g_object_unref(app);
    }
    
    int run() {
        char *dummy_argv[] = {(char*)"vidwall", nullptr};
        return g_application_run(G_APPLICATION(app), 1, dummy_argv);
    }
};

static gboolean on_focus_changed_main_thread(gpointer user_data) {
    auto *data = static_cast<FocusChangeData*>(user_data);
    
    // Clear the pending ID first
    *(data->pending_id) = 0;
    
    if (data->has_focus) {
        data->self->pause_video();
    } else {
        data->self->resume_video();
    }
    
    delete data;
    return G_SOURCE_REMOVE;
}

static void on_focus_changed(bool has_focus, HyprVidWall *self) {
    // Cancel any pending focus change callback
    if (self->pending_focus_change_id > 0) {
        g_source_remove(self->pending_focus_change_id);
        self->pending_focus_change_id = 0;
    }
    
    auto *data = new FocusChangeData{self, has_focus, &(self->pending_focus_change_id)};
    self->pending_focus_change_id = g_idle_add(on_focus_changed_main_thread, data);
}

int main(int argc, char **argv) {
    setenv("LC_NUMERIC", "C", 1);
    setlocale(LC_NUMERIC, "C");
    setlocale(LC_ALL, "C");
    
    g_args = CliArgs::parse(argc, argv);
    
    if (g_args.show_help) {
        CliArgs::print_help(argv[0]);
        return 0;
    }
    
    if (!g_args.is_valid()) {
        std::cerr << "\nUse --help for usage information" << std::endl;
        return 1;
    }
    
    HyprVidWall app(g_args);
    return app.run();
}
