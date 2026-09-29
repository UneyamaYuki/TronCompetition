/* generated common header file - do not edit */
#ifndef COMMON_DATA_H_
#define COMMON_DATA_H_
#include <stdint.h>
#include "bsp_api.h"
#include "rm_ethosu_api.h"
#include "rm_ethosu.h"
#include "arm_math.h"
#include "arm_nnfunctions.h"
#include "r_mipi_csi.h"
#include "r_mipi_csi_api.h"
#include "r_vin.h"
#include "r_capture_api.h"
#include "r_ioport.h"
#include "bsp_pin_cfg.h"
FSP_HEADER
#include "ethosu_driver.h"
extern struct ethosu_driver g_ethosu0;
extern rm_ethosu_instance_ctrl_t g_rm_ethosu0_ctrl;
extern const rm_ethosu_cfg_t g_rm_ethosu0_cfg;
extern const rm_ethosu_instance_t g_rm_ethosu0;
#ifndef NULL
void NULL(rm_ethosu_callback_args_t *p_arg);
#endif
/* MIPI PHY on MIPI PHY Instance. */

extern const mipi_phy_instance_t g_mipi_phy0;

/* Access the MIPI PHY instance using these structures when calling API functions directly (::p_api is not used). */
extern mipi_phy_ctrl_t g_mipi_phy0_ctrl;
extern const mipi_phy_cfg_t g_mipi_phy0_cfg;
/* MIPI CSI on MIPI CSI Instance. */
extern const mipi_csi_instance_t g_mipi_csi0;

/* Access the MIPI CSI instance using these structures when calling API functions directly (::p_api is not used). */
extern mipi_csi_instance_ctrl_t g_mipi_csi0_ctrl;
extern const mipi_csi_cfg_t g_mipi_csi0_cfg;

#ifndef fish_bbox_mipi_csi_callback
void fish_bbox_mipi_csi_callback(mipi_csi_callback_args_t *p_args);
#endif
/* MIPI VIN on MIPI VIN Instance. */
extern const capture_instance_t g_vin0;

/* Access the MIPI VIN instance using these structures when calling API functions directly (::p_api is not used). */
extern vin_instance_ctrl_t g_vin0_ctrl;
extern const capture_cfg_t g_vin0_cfg;

#ifndef fish_bbox_vin_callback
void fish_bbox_vin_callback(capture_callback_args_t *p_args);
#endif

#ifndef VIN_CFG_IMAGE_STRIDE
#define VIN_CFG_IMAGE_STRIDE (640)
#endif

#ifndef VIN_CFG_BYTES_PER_LINE
#define VIN_CFG_BYTES_PER_LINE (1280)
#endif

#define VIN_BYTES_PER_FRAME (VIN_CFG_BYTES_PER_LINE * 600)

extern uint8_t vin_image_buffer_1[VIN_BYTES_PER_FRAME];
extern uint8_t vin_image_buffer_2[VIN_BYTES_PER_FRAME];
extern uint8_t vin_image_buffer_3[VIN_BYTES_PER_FRAME];

#define IOPORT_CFG_NAME g_bsp_pin_cfg
#define IOPORT_CFG_OPEN R_IOPORT_Open
#define IOPORT_CFG_CTRL g_ioport_ctrl

/* IOPORT Instance */
extern const ioport_instance_t g_ioport;

/* IOPORT control structure. */
extern ioport_instance_ctrl_t g_ioport_ctrl;
void g_common_init(void);
FSP_FOOTER
#endif /* COMMON_DATA_H_ */
