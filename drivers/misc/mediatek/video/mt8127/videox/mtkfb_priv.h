#ifndef __MTKFB_PRIV_H
#define __MTKFB_PRIV_H

#include <linux/fb.h>

extern unsigned int isAEEEnabled;

/*********************************************************************/
/********************** Used for V4L2 ********************************/
/*********************************************************************/
#define DISPLAY_V4L2 0

#if DISPLAY_V4L2

#define MTKFB_QUEUE_OVERLAY_CONFIG_V4L2  _IOW('O', 240, struct fb_overlay_config)

typedef int (*HOOK_PAN_DISPLAY_IMPL) (struct fb_var_screeninfo *var, struct fb_info *info);
typedef void (*BUF_NOTIFY_CALLBACK) (int mva);
extern HOOK_PAN_DISPLAY_IMPL mtkfb_hook_pan_display;
extern HOOK_PAN_DISPLAY_IMPL mtkfb_1_hook_pan_display;
extern bool g_is_v4l2_active;
extern BUF_NOTIFY_CALLBACK hdmitx_buf_receive_cb;
extern BUF_NOTIFY_CALLBACK hdmitx_buf_remove_cb;
extern BUF_NOTIFY_CALLBACK mtkfb_buf_receive_cb;
extern BUF_NOTIFY_CALLBACK mtkfb_buf_remove_cb;

int mtkfb_1_update_res(struct fb_info *fbi, unsigned int xres, unsigned int yres);

#endif

#endif				/* __MTKFB_PRIV_H */
