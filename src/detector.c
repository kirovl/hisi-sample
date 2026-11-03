/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include "ss_mpi_sys.h"
#include "ss_mpi_sys_mem.h"
#include "ss_mpi_aidetect.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODEL_COUNT 3U
#define OBJECTS_PER_CLASS 64U
#define MODEL_PATH_SIZE 1024U

struct detect_model {
    int created;
    ot_aidetect_chn id;
    ot_aidetect_model_info info;
    ot_video_frame frame;
    td_phys_addr_t physical;
    void *memory;
    cam_image image;
    ot_aidetect_result_array result;
    ot_aidetect_object objects[OT_AIDETECT_CLASS_BUTT][OBJECTS_PER_CLASS];
};

struct cam_detector {
    unsigned int count;
    float threshold;
    struct detect_model models[MODEL_COUNT];
};

static int ai_check(td_s32 status, const char *operation)
{
    if (status != TD_SUCCESS) {
        fprintf(stderr, "%s failed: 0x%08x\n", operation, (unsigned int)status);
        return -1;
    }
    return 0;
}

void cam_detector_close(struct cam_detector *detector)
{
    if (detector == NULL) {
        return;
    }
    for (unsigned int i = detector->count; i > 0; --i) {
        struct detect_model *model = &detector->models[i - 1];
        if (model->memory != NULL) {
            ai_check(ss_mpi_sys_mmz_free(model->physical, model->memory), "free model input");
        }
        if (model->created) {
            ai_check(ss_mpi_aidetect_destroy_chn(model->id), "destroy detection channel");
        }
    }
    free(detector);
}

static int model_open(struct detect_model *model, const char *path, float threshold)
{
    ot_aidetect_chn_attr attr = {0};
    ot_aidetect_input_model input = {0};
    ot_aidetect_chn_param param = {0};
    uint32_t stride;
    uint32_t size;
    input.model_load_mode = OT_AIDETECT_MODEL_LOAD_FROM_PATH;
    input.model = (void *)path;
    input.size = (td_u32)strlen(path) + 1;
    if (ai_check(ss_mpi_aidetect_create_chn(model->id, &input, &attr), "create detection channel") != 0) {
        return -1;
    }
    model->created = 1;
    if (ai_check(ss_mpi_aidetect_get_model_info(model->id, &model->info), "read model info") != 0 ||
        model->info.class_num == 0 || model->info.class_num >= OT_AIDETECT_CLASS_BUTT ||
        model->info.size.width < 2 || model->info.size.height < 2 ||
        model->info.size.width > CAM_MAX_IMAGE_SIZE || model->info.size.height > CAM_MAX_IMAGE_SIZE ||
        (model->info.size.width & 1) || (model->info.size.height & 1)) {
        return -1;
    }
    if (ai_check(ss_mpi_aidetect_get_chn_param(model->id, &param), "get detection threshold") != 0 ||
        param.detect_threshold_num > OT_AIDETECT_CLASS_BUTT) {
        return -1;
    }
    param.detect_threshold_num = model->info.class_num;
    for (unsigned int i = 0; i < model->info.class_num; ++i) {
        if (model->info.classes[i] < 0 || model->info.classes[i] >= OT_AIDETECT_CLASS_BUTT) {
            return -1;
        }
        param.detect_threshold[i].class_type = model->info.classes[i];
        param.detect_threshold[i].detect_threshold = threshold;
        param.detect_threshold[i].track_miss_frame_num = 0;
    }
    if (ai_check(ss_mpi_aidetect_set_chn_param(model->id, &param), "set detection threshold") != 0) {
        return -1;
    }
    stride = (model->info.size.width + 15U) & ~15U;
    size = stride * model->info.size.height * 3U / 2U;
    if (ai_check(ss_mpi_sys_mmz_alloc(&model->physical, &model->memory,
        "cam_ai_input", NULL, size), "allocate model input") != 0) {
        return -1;
    }
    model->image = (cam_image){model->info.size.width, model->info.size.height,
        stride, stride, model->memory, (uint8_t *)model->memory + stride * model->info.size.height};
    model->frame.width = model->image.width;
    model->frame.height = model->image.height;
    model->frame.stride[0] = stride;
    model->frame.stride[1] = stride;
    model->frame.phys_addr[0] = model->physical;
    model->frame.phys_addr[1] = model->physical + stride * model->image.height;
    model->frame.virt_addr[0] = model->image.y;
    model->frame.virt_addr[1] = model->image.vu;
    model->frame.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    model->frame.video_format = OT_VIDEO_FORMAT_LINEAR;
    model->frame.compress_mode = OT_COMPRESS_MODE_NONE;
    model->frame.dynamic_range = OT_DYNAMIC_RANGE_SDR8;
    model->result.class_num = model->info.class_num;
    for (unsigned int i = 0; i < model->info.class_num; ++i) {
        model->result.object_class[i].class_type = model->info.classes[i];
        model->result.object_class[i].object_capacity = OBJECTS_PER_CLASS;
        model->result.object_class[i].objects = model->objects[i];
    }
    printf("MODEL channel=%d path=%s input=%ux%u classes=%u\n", model->id,
        path, model->image.width, model->image.height, model->info.class_num);
    return 0;
}

int cam_detector_open(struct cam_detector **output, const char *model_dir, int all_models, float threshold)
{
    static const char *names[MODEL_COUNT] = {"det_hvf_hor.bin", "det_pet_hor.bin", "det_pkg_hor.bin"};
    struct cam_detector *detector;
    char path[MODEL_PATH_SIZE];
    if (output == NULL || model_dir == NULL || !isfinite(threshold) || threshold <= 0 || threshold >= 1) {
        return -1;
    }
    *output = NULL;
    detector = calloc(1, sizeof(*detector));
    if (detector == NULL) {
        return -1;
    }
    detector->threshold = threshold;
    detector->count = all_models ? MODEL_COUNT : 1;
    for (unsigned int i = 0; i < detector->count; ++i) {
        int len = snprintf(path, sizeof(path), "%s/%s", model_dir, names[i]);
        detector->models[i].id = (ot_aidetect_chn)i;
        if (len < 0 || (size_t)len >= sizeof(path) ||
            model_open(&detector->models[i], path, threshold) != 0) {
            cam_detector_close(detector);
            return -1;
        }
    }
    *output = detector;
    return 0;
}

static void collect_objects(struct cam_detector *detector, struct detect_model *model,
    const cam_image *image, cam_result *result)
{
    cam_viewport view = {0, 0, (int)image->width, (int)image->height};
    for (unsigned int i = 0; i < model->info.class_num; ++i) {
        const ot_aidetect_object_of_one_class *source = &model->result.object_class[i];
        unsigned int count = source->object_num;
        if (count > OBJECTS_PER_CLASS) {
            result->dropped += count - OBJECTS_PER_CLASS;
            count = OBJECTS_PER_CLASS;
        }
        for (unsigned int j = 0; j < count; ++j) {
            const ot_aidetect_object *obj = &model->objects[i][j];
            cam_viewport box;
            if (!isfinite(obj->detect_confidence) || obj->detect_confidence < detector->threshold ||
                obj->detect_confidence > 1 || obj->detect_rect.width > INT32_MAX ||
                obj->detect_rect.height > INT32_MAX) {
                continue;
            }
            cam_object mapped = {(int)model->info.classes[i], obj->detect_confidence,
                obj->detect_rect.x, obj->detect_rect.y,
                (int32_t)obj->detect_rect.width, (int32_t)obj->detect_rect.height};
            if (cam_map_box(&mapped, &model->image, &view, &box) != 0) {
                continue;
            }
            if (result->count >= CAM_MAX_OBJECTS) {
                result->dropped++;
                continue;
            }
            mapped.x = box.x;
            mapped.y = box.y;
            mapped.width = box.width;
            mapped.height = box.height;
            result->objects[result->count++] = mapped;
        }
    }
}

int cam_detector_process(struct cam_detector *detector, const cam_image *image, cam_result *result)
{
    if (detector == NULL || result == NULL || !cam_image_valid(image)) {
        return -1;
    }
    result->count = 0;
    result->dropped = 0;
    for (unsigned int i = 0; i < detector->count; ++i) {
        struct detect_model *model = &detector->models[i];
        for (unsigned int j = 0; j < model->info.class_num; ++j) {
            model->result.object_class[j].object_num = 0;
        }
        if (cam_resize_nv21(image, &model->image) != 0 ||
            ai_check(ss_mpi_aidetect_process(model->id, &model->frame, &model->result), "detect frame") != 0) {
            result->count = 0;
            result->dropped = 0;
            return -1;
        }
        collect_objects(detector, model, image, result);
    }
    return 0;
}
