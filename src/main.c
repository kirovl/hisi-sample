/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include "ss_mpi_sys.h"
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct options {
    const char *models;
    const char *device;
    const char *input;
    const char *snapshot;
    const char *screenshot_dir;
    const char *touch;
    int rotation;
    unsigned int width;
    unsigned int height;
    unsigned int frames;
    unsigned int seconds;
    unsigned int fps;
    float threshold;
    int all_models;
    int headless;
};

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_snapshot_requested;

static void handle_signal(int signal_number)
{
    if (signal_number == SIGUSR1) g_snapshot_requested = 1;
    else g_stop = 1;
}

static int cancelled(void) { return g_stop != 0; }

static double now_seconds(void)
{
    struct timespec time_value;
    if (clock_gettime(CLOCK_MONOTONIC, &time_value) != 0) {
        return 0;
    }
    return time_value.tv_sec + time_value.tv_nsec / 1000000000.0;
}

static void usage(const char *name)
{
    printf("Usage: %s [--models-dir DIR] [--models hvf|all] [--threshold 0.5]\n"
        "  [--device /dev/dri/card0] [--headless] [--frames N] [--seconds N] [--fps 15]\n"
        "  [--rotation 90|270|0] [--touch /dev/input/eventX] [--screenshot-dir DIR]\n"
        "  [--input IMAGE.nv21 --width W --height H] [--snapshot LAST_FRAME.ppm]\n"
        "Default: live OS04D10 Sensor0 -> multi-object detection -> JD9853 240x320.\n"
        "File input is tightly packed NV21; headless never opens the display.\n", name);
}

static int parse_uint(const char *text, unsigned int maximum, unsigned int *value)
{
    char *end;
    unsigned long number;
    errno = 0;
    number = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || *text == '-' || number == 0 || number > maximum) {
        return -1;
    }
    *value = (unsigned int)number;
    return 0;
}

static int parse_options(int argc, char **argv, struct options *options)
{
    *options = (struct options){.models = "./models", .device = "/dev/dri/card0",
        .fps = 15, .threshold = 0.5f, .rotation = 90,
        .screenshot_dir = "/opt/camera_ai_lcd/screenshots"};
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            usage(argv[0]);
            return 1;
        }
        if (strcmp(arg, "--headless") == 0) {
            options->headless = 1;
            continue;
        }
        if (++i >= argc) {
            fprintf(stderr, "Missing value for %s\n", arg);
            return -1;
        }
        if (strcmp(arg, "--models-dir") == 0) {
            options->models = argv[i];
        } else if (strcmp(arg, "--device") == 0) {
            options->device = argv[i];
        } else if (strcmp(arg, "--input") == 0) {
            options->input = argv[i];
        } else if (strcmp(arg, "--snapshot") == 0) {
            options->snapshot = argv[i];
        } else if (strcmp(arg, "--screenshot-dir") == 0) {
            options->screenshot_dir = argv[i];
        } else if (strcmp(arg, "--touch") == 0) {
            options->touch = argv[i];
        } else if (strcmp(arg, "--rotation") == 0) {
            if (strcmp(argv[i], "90") == 0) options->rotation = 90;
            else if (strcmp(argv[i], "270") == 0) options->rotation = 270;
            else if (strcmp(argv[i], "0") == 0) options->rotation = 0;
            else return -1;
        } else if (strcmp(arg, "--models") == 0) {
            if (strcmp(argv[i], "all") != 0 && strcmp(argv[i], "hvf") != 0) {
                return -1;
            }
            options->all_models = strcmp(argv[i], "all") == 0;
        } else if (strcmp(arg, "--threshold") == 0) {
            char *end;
            errno = 0;
            options->threshold = strtof(argv[i], &end);
            if (errno != 0 || end == argv[i] || *end != '\0' || !isfinite(options->threshold) ||
                options->threshold <= 0 || options->threshold >= 1) {
                return -1;
            }
        } else if (strcmp(arg, "--width") == 0) {
            if (parse_uint(argv[i], CAM_MAX_IMAGE_SIZE, &options->width) != 0) {
                return -1;
            }
        } else if (strcmp(arg, "--height") == 0) {
            if (parse_uint(argv[i], CAM_MAX_IMAGE_SIZE, &options->height) != 0) {
                return -1;
            }
        } else if (strcmp(arg, "--frames") == 0) {
            if (parse_uint(argv[i], 1000000, &options->frames) != 0) {
                return -1;
            }
        } else if (strcmp(arg, "--seconds") == 0) {
            if (parse_uint(argv[i], 86400, &options->seconds) != 0) {
                return -1;
            }
        } else if (strcmp(arg, "--fps") == 0) {
            if (parse_uint(argv[i], 30, &options->fps) != 0) {
                return -1;
            }
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            return -1;
        }
    }
    if (options->input != NULL && (options->width < 2 || options->height < 2 ||
        (options->width & 1U) || (options->height & 1U))) {
        fprintf(stderr, "File input requires even --width and --height\n");
        return -1;
    }
    if (options->input != NULL && options->frames == 0 && options->seconds == 0) {
        options->frames = 1;
    }
    return 0;
}

static int load_image(const struct options *options, cam_image *image, uint8_t **allocation)
{
    size_t pixels = (size_t)options->width * options->height;
    size_t size = pixels * 3 / 2;
    FILE *file = fopen(options->input, "rb");
    if (file == NULL) {
        perror(options->input);
        return -1;
    }
    *allocation = malloc(size);
    if (*allocation == NULL) {
        fclose(file);
        return -1;
    }
    size_t got = fread(*allocation, 1, size, file);
    int extra = fgetc(file);
    int failed = ferror(file);
    fclose(file);
    if (got != size || extra != EOF || failed) {
        fprintf(stderr, "Expected exactly %zu bytes of packed NV21 in %s\n", size, options->input);
        return -1;
    }
    *image = (cam_image){options->width, options->height, options->width,
        options->width, *allocation, *allocation + pixels};
    return 0;
}

static int save_ppm(const char *path, const cam_canvas *canvas)
{
    FILE *file = fopen(path, "wb");
    int status = 0;
    if (file == NULL) {
        perror(path);
        return -1;
    }
    if (fprintf(file, "P6\n%u %u\n255\n", canvas->width, canvas->height) < 0) {
        status = -1;
    }
    for (uint32_t y = 0; y < canvas->height && status == 0; ++y) {
        for (uint32_t x = 0; x < canvas->width; ++x) {
            uint32_t pixel = canvas->pixels[(size_t)y * canvas->stride + x];
            unsigned char rgb[3] = {(unsigned char)(pixel >> 16),
                (unsigned char)(pixel >> 8), (unsigned char)pixel};
            if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb)) {
                status = -1;
                break;
            }
        }
    }
    if (fclose(file) != 0) {
        status = -1;
    }
    return status;
}

int main(int argc, char **argv)
{
    struct options options;
    struct cam_camera *camera = NULL;
    struct cam_detector *detector = NULL;
    struct cam_display *display = NULL;
    struct cam_touch *touch = NULL;
    cam_stats stats = {0};
    cam_menu menu = {.x=296,.y=16,.ai_enabled=1};
    cam_device_info device_info = {0};
    int info_page = -1;
    double info_updated = 0;
    double total_capture = 0, total_ai = 0, total_render = 0, total_display = 0;
    cam_canvas headless = {0};
    cam_canvas *canvas = NULL;
    cam_image image = {0};
    cam_result result = {0};
    uint8_t *file_memory = NULL;
    unsigned int completed = 0;
    int file_sys_ready = 0;
    int exit_code = EXIT_FAILURE;
    double mode_active = 0;
    unsigned int mode_frames = 0;
    double start;
    int parsed = parse_options(argc, argv, &options);
    if (parsed != 0) {
        if (parsed < 0) {
            usage(argv[0]);
        }
        return parsed > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct sigaction action = {0};
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 || sigaction(SIGTERM, &action, NULL) != 0 || sigaction(SIGUSR1, &action, NULL) != 0) {
        perror("sigaction");
        goto cleanup;
    }
    if (options.headless) {
        headless = (cam_canvas){NULL, options.rotation == 0 ? 240 : 320,
            options.rotation == 0 ? 320 : 240, options.rotation == 0 ? 240 : 320, 240U * 320U};
        headless.pixels = calloc(headless.capacity, sizeof(*headless.pixels));
        canvas = &headless;
        if (headless.pixels == NULL) {
            goto cleanup;
        }
    } else {
        if (cam_display_open(&display, options.device, options.rotation) != 0) {
            goto cleanup;
        }
        canvas = cam_display_canvas(display);
        if (cam_touch_open(&touch, options.touch, options.rotation) == 0) stats.touch_ready = 1;
        else {
            fprintf(stderr, "Touch input not available; SNAPSHOT button disabled, SIGUSR1 still saves screenshots.\n");
            if (options.touch != NULL) goto cleanup;
        }
    }
    if (!options.headless) {
        cam_canvas_clear(canvas,0x000b1823);
        cam_canvas_text(canvas,12,85,"STARTING CAMERA AND AI...",0x00e5f3f5);
        if(cam_display_present(display)!=0)goto cleanup;
    }
    if (options.input != NULL) {
        if (load_image(&options, &image, &file_memory) != 0 || ss_mpi_sys_init() != TD_SUCCESS) {
            fprintf(stderr, "File input/SYS initialization failed\n");
            goto cleanup;
        }
        file_sys_ready = 1;
    } else if (cam_camera_open(&camera) != 0) {
        goto cleanup;
    }
    if (cam_detector_open(&detector, options.models, options.all_models, options.threshold) != 0) {
        goto cleanup;
    }
    start = now_seconds();
    exit_code = EXIT_SUCCESS;
    while (!g_stop && (options.frames == 0 || completed < options.frames) &&
        (options.seconds == 0 || now_seconds() - start < options.seconds)) {
        double frame_start = now_seconds();
        for(int i=0;i<32;i++) {
            cam_touch_event event;
            int kind=cam_touch_poll_motion(touch,&event);
            if(kind<0){fprintf(stderr,"Touch read failed\n");g_stop=1;exit_code=EXIT_FAILURE;break;}
            if(!kind)break;
            if(info_page>=0) {
                if(kind==1){
                    if(event.y<29&&event.x>=250)info_page=-1;
                    else if(event.y>=34&&event.y<58&&event.x>=0&&event.x<320){info_page=event.x/80;info_updated=0;}
                }
            } else {
                int selected=cam_menu_event(&menu,&event,canvas->width,canvas->height);
                if(selected==1){info_page=0;info_updated=0;}
                else if(selected==3){
                    if(cam_wifi_setup(display,touch,cancelled)<0)fprintf(stderr,"Wi-Fi setup unavailable; returning to preview.\n");
                    break;
                }else if(selected==4)g_snapshot_requested=1;
                else if(selected==5){
                    mode_active=0;mode_frames=0;
                    printf("AI_TOGGLE enabled=%d\n",menu.ai_enabled);
                }
            }
        }
        if(g_stop)break;
        stats.show_overlay=menu.show_stats;
        stats.ai_enabled=menu.ai_enabled;
        frame_start=now_seconds(); /* Exclude time spent in menus from camera/AI timings. */
        if (camera != NULL && cam_camera_get(camera, &image) != 0) {
            exit_code = EXIT_FAILURE;
            break;
        }
        double captured = now_seconds();
        total_capture += captured - frame_start;
        cam_camera_stats(camera, &stats);
        double ai_start = now_seconds();
        memset(&result,0,sizeof(result));
        if (menu.ai_enabled && cam_detector_process(detector, &image, &result) != 0) {
            exit_code = EXIT_FAILURE;
            cam_camera_release(camera);
            break;
        }
        double detected = now_seconds();
        total_ai += detected - ai_start;
        double measured_fps = mode_active > 0 ? mode_frames / mode_active : 0;
        stats.processing_fps = measured_fps;
        if (cam_render_ui(canvas, &image, &result, &stats) != 0) {
            exit_code = EXIT_FAILURE; cam_camera_release(camera); break;
        }
        /* Save the camera/result layer before adding any navigation UI. */
        if (g_snapshot_requested) {
            g_snapshot_requested = 0;
            if (cam_save_capture(options.screenshot_dir, canvas, &image, &result, &stats, stats.saved + 1) == 0)
                ++stats.saved;
            else perror("Screenshot save failed");
        }
        if(info_page>=0){
            if(info_updated==0||now_seconds()-info_updated>=5){cam_device_info_read(&device_info,info_page,&stats);info_updated=now_seconds();}
            cam_device_info_draw(canvas,&device_info,info_page);
        } else if(display!=NULL)cam_menu_draw(canvas,&menu);
        double rendered = now_seconds();
        total_render += rendered - detected;
        if (display != NULL && cam_display_present(display) != 0) {
            exit_code = EXIT_FAILURE; cam_camera_release(camera); break;
        }
        total_display += now_seconds() - rendered;
        cam_camera_release(camera);
        completed++;
        printf("FRAME %u ai=%s objects=%zu dropped=%u source=%ux%u camera=%ux%u camera_fps=%.2f\n", completed,
            menu.ai_enabled ? "on" : "off", result.count, result.dropped, image.width, image.height,
            stats.sensor_width, stats.sensor_height, stats.sensor_fps);
        if (completed % 15 == 0)
            printf("PROFILE frames=%u capture_ms=%.2f ai_ms=%.2f render_ms=%.2f display_ms=%.2f\n", completed,
                total_capture * 1000 / completed, total_ai * 1000 / completed,
                total_render * 1000 / completed, total_display * 1000 / completed);
        for (size_t i = 0; i < result.count; ++i) {
            const cam_object *obj = &result.objects[i];
            printf("OBJECT %s confidence=%.3f box=%d,%d,%d,%d\n", cam_class_name(obj->type),
                obj->confidence, obj->x, obj->y, obj->width, obj->height);
        }
        double pause = 1.0 / options.fps - (now_seconds() - frame_start);
        if (pause > 0 && !g_stop) {
            struct timespec delay = {(time_t)pause, (long)((pause - (time_t)pause) * 1000000000.0)};
            while (nanosleep(&delay, &delay) != 0 && errno == EINTR && !g_stop) {
                /* Resume only the unelapsed part of the frame period. */
            }
        }
        mode_active += now_seconds() - frame_start;
        ++mode_frames;
        if(mode_frames % 30 == 0)
            printf("MODE_PROFILE ai=%s frames=%u elapsed=%.3f fps=%.2f\n",
                menu.ai_enabled?"on":"off",mode_frames,mode_active,mode_frames/mode_active);
    }
    if (completed == 0) {
        exit_code = EXIT_FAILURE;
    }
    if (options.snapshot != NULL && completed > 0) {
        size_t length = strlen(options.snapshot);
        int rc = length >= 4 && strcmp(options.snapshot + length - 4, ".png") == 0 ?
            cam_save_png(options.snapshot, canvas) : save_ppm(options.snapshot, canvas);
        if (rc != 0) exit_code = EXIT_FAILURE;
    }
    if (completed > 0)
        printf("PROFILE frames=%u capture_ms=%.2f ai_ms=%.2f render_ms=%.2f display_ms=%.2f\n", completed,
            total_capture * 1000 / completed, total_ai * 1000 / completed,
            total_render * 1000 / completed, total_display * 1000 / completed);
    double duration = now_seconds() - start;
    printf("SUMMARY frames=%u elapsed=%.3f fps=%.2f status=%s mode=%s\n", completed,
        duration, duration > 0 ? completed / duration : 0,
        exit_code == EXIT_SUCCESS ? "ok" : "failed", options.input != NULL ? "file" : "camera");
cleanup:
    cam_touch_close(touch);
    cam_detector_close(detector);
    cam_camera_close(camera);
    if (file_sys_ready) {
        ss_mpi_sys_exit();
    }
    cam_display_close(display);
    free(headless.pixels);
    free(file_memory);
    return exit_code;
}
