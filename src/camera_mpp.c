/* SPDX-License-Identifier: Apache-2.0 */
#include "camera_ai_lcd.h"
#include "sample_comm.h"
#include "ss_mpi_sys.h"
#include "ss_mpi_vpss.h"
#include "ss_mpi_ae.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cam_camera {
    sample_vi_cfg vi;
    ot_video_frame_info frame;
    void *y;
    void *vu;
    td_u32 y_size;
    td_u32 vu_size;
    ot_size sensor_size;
    int sys_ready;
    int vi_ready;
    int grp_ready;
    int chn_ready;
    int grp_started;
    int bound;
    int held;
};

static int check_mpi(td_s32 status, const char *operation)
{
    if (status != TD_SUCCESS) {
        fprintf(stderr, "%s failed: 0x%08x\n", operation, (unsigned int)status);
        return -1;
    }
    return 0;
}

void cam_camera_release(struct cam_camera *camera)
{
    if (camera == NULL) {
        return;
    }
    if (camera->vu != NULL) {
        ss_mpi_sys_munmap(camera->vu, camera->vu_size);
        camera->vu = NULL;
    }
    if (camera->y != NULL) {
        ss_mpi_sys_munmap(camera->y, camera->y_size);
        camera->y = NULL;
    }
    if (camera->held) {
        check_mpi(ss_mpi_vpss_release_chn_frame(0, 0, &camera->frame), "release VPSS frame");
        camera->held = 0;
    }
}

void cam_camera_close(struct cam_camera *camera)
{
    if (camera == NULL) {
        return;
    }
    cam_camera_release(camera);
    if (camera->bound) {
        sample_comm_vi_un_bind_vpss(0, 0, 0, 0);
    }
    if (camera->grp_started) {
        ss_mpi_vpss_stop_grp(0);
    }
    if (camera->chn_ready) {
        ss_mpi_vpss_disable_chn(0, 0);
    }
    if (camera->grp_ready) {
        ss_mpi_vpss_destroy_grp(0);
    }
    if (camera->vi_ready) {
        sample_comm_vi_stop_vi(&camera->vi);
    }
    if (camera->sys_ready) {
        sample_comm_sys_exit();
    }
    free(camera);
}

int cam_camera_open(struct cam_camera **output)
{
    struct cam_camera *camera;
    ot_size sensor_size = {0};
    sample_vb_param pools = {0};
    ot_vb_cfg vb_cfg = {0};
    ot_vpss_grp_attr grp_attr;
    ot_vpss_chn_attr chn_attr;
    unsigned int i;
    if (output == NULL) {
        return -1;
    }
    *output = NULL;
    camera = calloc(1, sizeof(*camera));
    if (camera == NULL) {
        return -1;
    }
    sample_comm_vi_get_size_by_sns_type(SENSOR0_TYPE, &sensor_size);
    if (sensor_size.width == 0 || sensor_size.height == 0) {
        goto fail;
    }
    camera->sensor_size = sensor_size;
    pools.vb_size[0] = sensor_size;
    pools.vb_size[1] = sensor_size;
    pools.vb_size[2] = (ot_size){768, 432};
    for (i = 0; i < 3; ++i) {
        pools.pixel_format[i] = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        pools.compress_mode[i] = OT_COMPRESS_MODE_NONE;
        pools.video_format[i] = OT_VIDEO_FORMAT_LINEAR;
        pools.blk_num[i] = 4;
    }
    pools.pixel_format[0] = OT_PIXEL_FORMAT_RGB_BAYER_10BPP;
    pools.compress_mode[0] = OT_COMPRESS_MODE_LINE;
    sample_comm_sys_get_default_vb_cfg(&pools, &vb_cfg);
    if (check_mpi(sample_comm_sys_init_with_vb_supplement(&vb_cfg,
        OT_VB_SUPPLEMENT_BNR_MOT_MASK), "SYS/VB init") != 0) {
        goto fail;
    }
    camera->sys_ready = 1;
    if (check_mpi(sample_comm_vi_set_vi_vpss_mode(OT_VI_OFFLINE_VPSS_ONLINE,
        OT_VI_AIISP_MODE_DEFAULT), "VI/VPSS mode") != 0) {
        goto fail;
    }
    sample_comm_vi_get_default_vi_cfg(SENSOR0_TYPE, &camera->vi);
    sample_comm_vi_get_mipi_info_by_dev_id(SENSOR0_TYPE, 0, &camera->vi.mipi_info);
    camera->vi.sns_info.bus_id = 0;
    camera->vi.sns_info.sns_clk_src = 0;
    camera->vi.sns_info.sns_rst_src = 0;
    camera->vi.dev_info.vi_dev = 0;
    camera->vi.bind_pipe.pipe_id[0] = 0;
    camera->vi.pipe_info[0].isp_info.isp_pub_attr.frame_rate = 15;
    camera->vi.pipe_info[0].isp_be_end_trigger = TD_TRUE;
    if (check_mpi(sample_comm_vi_start_vi(&camera->vi), "start OS04D10 Sensor0") != 0) {
        goto fail;
    }
    camera->vi_ready = 1;
    sample_comm_vpss_get_default_grp_attr(&grp_attr);
    grp_attr.max_width = sensor_size.width;
    grp_attr.max_height = sensor_size.height;
    if (check_mpi(ss_mpi_vpss_create_grp(0, &grp_attr), "create VPSS") != 0) {
        goto fail;
    }
    camera->grp_ready = 1;
    sample_comm_vpss_get_default_chn_attr(&chn_attr);
    chn_attr.width = 768;
    chn_attr.height = 432;
    chn_attr.depth = 2;
    chn_attr.pixel_format = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    chn_attr.compress_mode = OT_COMPRESS_MODE_NONE;
    chn_attr.video_format = OT_VIDEO_FORMAT_LINEAR;
    if (check_mpi(ss_mpi_vpss_set_chn_attr(0, 0, &chn_attr), "set VPSS channel") != 0 ||
        check_mpi(ss_mpi_vpss_enable_chn(0, 0), "enable VPSS channel") != 0) {
        goto fail;
    }
    camera->chn_ready = 1;
    if (check_mpi(ss_mpi_vpss_start_grp(0), "start VPSS") != 0) {
        goto fail;
    }
    camera->grp_started = 1;
    if (check_mpi(sample_comm_vi_bind_vpss(0, 0, 0, 0), "bind VI to VPSS") != 0) {
        goto fail;
    }
    camera->bound = 1;
    *output = camera;
    return 0;
fail:
    cam_camera_close(camera);
    return -1;
}

int cam_camera_get(struct cam_camera *camera, cam_image *image)
{
    ot_video_frame *frame;
    if (camera == NULL || image == NULL || camera->held) {
        return -1;
    }
    if (check_mpi(ss_mpi_vpss_get_chn_frame(0, 0, &camera->frame, 2000),
        "get camera frame (check sensor and SW1301)") != 0) {
        return -1;
    }
    camera->held = 1;
    frame = &camera->frame.video_frame;
    if (frame->pixel_format != OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420 ||
        frame->compress_mode != OT_COMPRESS_MODE_NONE || frame->width == 0 ||
        frame->height == 0 || frame->width > CAM_MAX_IMAGE_SIZE ||
        frame->height > CAM_MAX_IMAGE_SIZE || (frame->width & 1) || (frame->height & 1) ||
        frame->stride[0] < frame->width || frame->stride[1] < frame->width ||
        frame->stride[0] > CAM_MAX_IMAGE_SIZE * 2 || frame->stride[1] > CAM_MAX_IMAGE_SIZE * 2) {
        fprintf(stderr, "Unsupported VPSS frame layout\n");
        cam_camera_release(camera);
        return -1;
    }
    camera->y_size = frame->stride[0] * frame->height;
    camera->vu_size = frame->stride[1] * (frame->height / 2);
    camera->y = ss_mpi_sys_mmap(frame->phys_addr[0], camera->y_size);
    camera->vu = ss_mpi_sys_mmap(frame->phys_addr[1], camera->vu_size);
    if (camera->y == NULL || camera->vu == NULL) {
        cam_camera_release(camera);
        return -1;
    }
    *image = (cam_image){frame->width, frame->height, frame->stride[0],
        frame->stride[1], camera->y, camera->vu};
    return 0;
}

void cam_camera_stats(struct cam_camera *camera, cam_stats *stats)
{
    if (camera == NULL || stats == NULL) return;
    stats->sensor_width = camera->sensor_size.width;
    stats->sensor_height = camera->sensor_size.height;
    ot_isp_exp_info exposure = {0};
    stats->sensor_fps = -1;
    /* Vendor sample_awb_calibration.c normalizes this field by 100. */
    if (ss_mpi_isp_query_exposure_info(0, &exposure) == TD_SUCCESS && exposure.fps > 0)
        stats->sensor_fps = exposure.fps / 100.0;
}
