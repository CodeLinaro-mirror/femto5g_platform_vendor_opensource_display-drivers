// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "wm_vtg.h"
#include "linux/math.h"


static int vtg_reg_read(struct wm_vtg *vtg, u32 off)
{
	struct wm_display *display;

	display = vtg->display;

	/*TODO: modify the func once regmap info is confirmed*/
	return display->read_register(display, off);
}

static int vtg_reg_write(struct wm_vtg *vtg, u32 off, u32 val)
{
	struct wm_display *display;
	display = vtg->display;

	/*TODO: modify the func once regmap info is confirmed*/
	return display->write_register(display, off, val);
}

static int wm_vtg_irq_handler(struct wm_vtg *vtg, int irq)
{
	int intr;

	if (!vtg) {
		WM_ERR("invalid param");
		return IRQ_NONE;
	}

	if ((WM_DISPLAY_INT_VTG_OBS_FIFO_LATENCY_GT_BOUND < irq) &&
		(irq < WM_DISPLAY_INT_VTG_FRAME_BUFFER_ALMOST_EMPTY))
		intr = 1;/*elastic buffer err*/

	else if ((irq == WM_DISPLAY_INT_VTG_GENERATOR_ERROR_INT) ||
			(irq == WM_DISPLAY_INT_VTG_FRAME_BUFFER_FULL) ||
			(irq == WM_DISPLAY_INT_VTG_FRAME_BUFFER_EMPTY))
		intr = 2;/*vtg err*/

	switch (intr) {
		/*elastic buffer errors*/
		case 1:
			if (!vtg->hw_enable) {
				/*SW elastic buffer mgmnt*/
				queue_work(vtg->workq, &vtg->buffer_work);
			}
			break;
		/*VTG errors*/
		case 2:
			/*reset the DSS pipeline*/
			queue_work(vtg->workq, &vtg->err_work);
			break;
		/*False case*/
		default:
			break;
	}

	WM_DEBUG("VTG interrupt %d handled\n", irq);
	return IRQ_HANDLED;
}

static void wm_vtg_configure_source(struct wm_vtg *vtg)
{
	struct wm_display_mode *mode = &vtg->display->mode_info;

	if (mode->dsc_enabled)
		vtg_reg_write(vtg, VTG_ENABLE, 0x2);
	else
		vtg_reg_write(vtg, VTG_ENABLE, 0x0);

	WM_INFO("VTG source configured with dsc %\n", mode->dsc_enabled);
}

static void wm_vtg_config_frame_parameters(struct wm_vtg *vtg)
{
	struct wm_display_mode *mode = &vtg->display->mode_info;
	u32 h_total;

	h_total = mode->h_active + mode->h_back_porch
			+ mode->h_sync_width + mode->h_front_porch;

	vtg_reg_write(vtg, VTG_VFP_LINES, (mode->v_front_porch & 0xFFFF));
	vtg_reg_write(vtg, VTG_VSA_LINES, (mode->v_sync_width & 0xFFFF));
	vtg_reg_write(vtg, VTG_VBP_LINES, (mode->v_back_porch & 0xFFFF));
	vtg_reg_write(vtg, VTG_VACT_LINES, (mode->v_active & 0xFFFF));
	vtg_reg_write(vtg, VTG_HFP_CYCLES, (mode->h_front_porch & 0xFFFF));
	vtg_reg_write(vtg, VTG_HSA_CYCLES, (mode->h_sync_width & 0xFFFF));
	vtg_reg_write(vtg, VTG_HBP_CYCLES, (mode->h_back_porch & 0xFFFF));
	vtg_reg_write(vtg, VTG_HACT_CYCLES, (mode->h_active & 0xFFFF));
	vtg_reg_write(vtg, VTG_TOTAL_PIXELS, (h_total & 0xFFFFF));

	WM_DEBUG("VTG frame paramters configured\n");
}

static void wm_vtg_set_elastic_buffer_watermarks(struct wm_vtg *vtg)
{

	/*Values are taken from projected values by DV*/
	vtg_reg_write(vtg, VTG_FRAME_DELTA_BOUND, 0X2710);
	vtg_reg_write(vtg, VTG_FRAME_DELTA_LP2_BOUND, 0X2710);
	vtg_reg_write(vtg, VTG_FRAME_DELTA_LP4_BOUND, 0X2710);
	vtg_reg_write(vtg, VTG_FRAME_DELTA_LP8_BOUND, 0X2710);
	vtg_reg_write(vtg, VTG_FRAME_LATENCY_BOUND, 0X03E8);
}

static void wm_vtg_clear_frame_statistics(struct wm_vtg *vtg)
{

	vtg_reg_write(vtg, VTG_FRAME_INTERVAL, 0x0);
	vtg_reg_write(vtg, VTG_FRAME_DELTA, 0X0);
	vtg_reg_write(vtg, VTG_FRAME_DELTA_LP2, 0X0);
	vtg_reg_write(vtg, VTG_FRAME_DELTA_LP4, 0X0);
	vtg_reg_write(vtg, VTG_FRAME_DELTA_LP8, 0X0);
	vtg_reg_write(vtg, VTG_FRAME_LATENCY, 0X0);
}

static void wm_vtg_handle_error_recovery(struct work_struct *work)
{
	struct wm_vtg *vtg;

	vtg = container_of(work, struct wm_vtg, err_work);

	WM_INFO("Resetting DSS pipeling\n");

	vtg->display->reset_video_path(vtg->display,
		WM_DISPLAY_RESET_REASON_VTG_ERROR);
}

static void wm_vtg_handle_sw_buffer_management(struct work_struct *work)
{
	struct wm_vtg *vtg;
	u32 Ptotal;
	u32 delta_err, delta_lp2_err, delta_lp4_err,
	    delta_lp8_err;
	u32 delta_ppm, delta_lp2_ppm, delta_lp4_ppm, delta_lp8_ppm;
	u32 new_freq = 0;
	u32 frame_postpone, frame_latency, lat_err, temp;
	int ret;

	vtg = container_of(work, struct wm_vtg, buffer_work);

	Ptotal = vtg_reg_read(vtg, VTG_TOTAL_PIXELS);

	/*add converison of delta into ppm and roundoff*/
	delta_err = vtg_reg_read(vtg, VTG_FRAME_DELTA);
	do_div(delta_err, Ptotal);
	delta_ppm = delta_err;
	do_div(delta_ppm, 1000000);

	/*LP2*/
	delta_lp2_err = vtg_reg_read(vtg, VTG_FRAME_DELTA_LP2);
	do_div(delta_lp2_err, Ptotal);
	delta_lp2_ppm = delta_lp2_err;
	do_div(delta_lp2_ppm, 1000000);

	/*LP4*/
	delta_lp4_err = vtg_reg_read(vtg, VTG_FRAME_DELTA_LP4);
	do_div(delta_lp4_err, Ptotal);
	delta_lp4_ppm = delta_lp4_err;
	do_div(delta_lp4_ppm, 1000000);

	/*LP8*/
	delta_lp8_err = vtg_reg_read(vtg, VTG_FRAME_DELTA_LP8);
	do_div(delta_lp8_err, Ptotal);
	delta_lp8_ppm = delta_lp8_err;
	do_div(delta_lp8_ppm, 1000000);

	if (abs(delta_ppm) > 100)
		new_freq = (new_freq * (1 - delta_err));

	else if (abs(delta_lp2_ppm) > 50)
		new_freq = (new_freq * (1 - delta_lp2_err));

	else if (abs(delta_lp4_ppm) > 10)
		new_freq = (new_freq * (1 - delta_lp4_err));

	else
		new_freq = (new_freq * (1 - delta_lp8_err));

	frame_latency = vtg_reg_read(vtg, VTG_FRAME_LATENCY);
	frame_postpone = vtg_reg_read(vtg, VTG_FRAME_POSTPONE);
	temp = frame_latency - frame_postpone;

	lat_err = do_div(temp, Ptotal);
	temp =  (1 + lat_err);
	do_div(temp, 10);

	new_freq = new_freq * temp;

	ret = vtg->display->set_video_clk_rate(vtg->display, new_freq);

	/*call directly clk apis or enable pixel pll*/
	WM_INFO("VTG SW buffer mgmnt set with freq: %lu\n", new_freq);

}

static void wm_vtg_set_hw_buffer_management(struct wm_vtg *vtg)
{

	/*buffer mgmnt done by HW*/
	vtg_reg_write(vtg, VTG_HW_K_PERIOD, 0x0);
	vtg_reg_write(vtg, VTG_HW_K_LATENCY, 0x0);
	vtg_reg_write(vtg, VTG_HW_FEEDBACK, 0x1);

	WM_DEBUG("VTG HW buffer mngment set \n");
	return;
}

static int _wm_vtg_create_workqueue(struct wm_vtg *vtg)
{
	if (!vtg)
		return -ENOMEM;

	vtg->workq = create_singlethread_workqueue("wm_vtg_wq");

	if (IS_ERR_OR_NULL(vtg->workq)) {
		WM_ERR("creating workqueue failed\n");
		return -ENOMEM;
	}

	if (!vtg->hw_enable)
		INIT_WORK(&vtg->buffer_work,
				wm_vtg_handle_sw_buffer_management);

	INIT_WORK(&vtg->err_work, wm_vtg_handle_error_recovery);

	return 0;
}

static void _wm_vtg_enable(struct wm_vtg *vtg, bool enable)
{

	u32 data;

	if (enable) {
		data = vtg_reg_read(vtg, VTG_ENABLE);
		data = data | 0x1;
		/*TODO: add hdmi polarity*/
		vtg_reg_write(vtg, VTG_ENABLE, data);
	} else {
		vtg_reg_write(vtg, VTG_ENABLE, 0x0);
	}

}

static int wm_vtg_pre_enable(struct wm_vtg *vtg)
{

	/* Configure vtg with below details.
	 *
	 * 1. Configure input source,
	 * 2. Configure frame parameters,
	 * 3. Clear frame statistics,
	 * 4. Enable vtg
	 */
	wm_vtg_configure_source(vtg);

	wm_vtg_config_frame_parameters(vtg);

	wm_vtg_clear_frame_statistics(vtg);

	if (vtg->hw_enable)
		wm_vtg_set_hw_buffer_management(vtg);
	else
		wm_vtg_set_elastic_buffer_watermarks(vtg);		
		
	WM_DEBUG("VTG pre-enable successfull\n");

	return 0;
}

void wm_vtg_deinit(struct wm_vtg *vtg)
{

	if (vtg->workq) {
		cancel_work_sync(&vtg->buffer_work);
		cancel_work_sync(&vtg->err_work);
		destroy_workqueue(vtg->workq);
	}

	WM_DEBUG("VTG deinit\n");
}

static int wm_vtg_enable(struct wm_vtg *vtg)
{
	_wm_vtg_enable(vtg, true);

	WM_INFO("VTG enable succesfull\n");
	return 0;
}

static int wm_vtg_disable(struct wm_vtg *vtg)
{
	_wm_vtg_enable(vtg, false);

	wm_vtg_clear_frame_statistics(vtg);

	WM_DEBUG("VTG disabled\n");

	return 0;
}

struct wm_vtg *wm_vtg_init(struct wm_display_info *display_info)
{
	struct wm_vtg *wm_vtg;
	int rc;

	if(!display_info || !display_info->dev
			|| !display_info->display) {
		WM_ERR("VTG: invalid arguments");
		rc = -ENODEV;
		goto error;
	}

	wm_vtg = devm_kzalloc(display_info->dev, sizeof(*wm_vtg), GFP_KERNEL);

	if (!wm_vtg) {
		rc = -ENOMEM;
		goto error;
	}

	wm_vtg->display = display_info->display;

	/*setting hw buffer mgmnt*/
	wm_vtg->hw_enable = true;

	/*assign function pointers*/
	wm_vtg->pre_enable = wm_vtg_pre_enable;
	wm_vtg->enable = wm_vtg_enable;
	wm_vtg->disable = wm_vtg_disable;
	wm_vtg->deinit = wm_vtg_deinit;
	wm_vtg->irq_handler = wm_vtg_irq_handler;
	/*initialize workqueue*/
	_wm_vtg_create_workqueue(wm_vtg);

	return wm_vtg;
error:
	WM_ERR("VTG initialization failed\n");
	return ERR_PTR(rc);
}


