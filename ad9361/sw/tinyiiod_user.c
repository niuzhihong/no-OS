/*
 * libtinyiiod - Tiny IIO Daemon Library
 *
 * Copyright (C) 2016 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "tinyiiod.h"
#include "tinyiiod_user.h"
#include "adc_core.h"
#include "dac_core.h"
#include "parameters.h"
#include "xil_io.h"
#include "ad9361_api.h"
#include "serial.h"
#include "xil_cache.h"

static unsigned int addr_to_read;
static uint32_t request_mask;
// mask for cf-ad9361-lpc 0x0F, it has 4 channels
static uint32_t input_channel_mask = 0x0F;
extern struct ad9361_rf_phy *ad9361_phy;
extern struct adc_state adc_st;
int einval(void){
	volatile int x=3;
	x++;
	return 22;}
int enoent(void){
	volatile int x=3;
	x++;
	return 2;
}

static const char * const ad9361_agc_modes[] =
 	{"manual", "fast_attack", "slow_attack", "hybrid"};

static const char * const ad9361_rf_rx_port[] =
	{"A_BALANCED", "B_BALANCED", "C_BALANCED",
	 "A_N", "A_P", "B_N", "B_P", "C_N", "C_P", "TX_MONITOR1",
	 "TX_MONITOR2", "TX_MONITOR1_2"};
static const char * const ad9361_rf_tx_port[] =
	{"A", "B"};

/***********************************************************************************************************************
* Function Name: read_data
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static int read_line(char *buf)
{
	return serial_read_line(buf);
}

static int read(char *buf, size_t len) {
	return serial_read(buf, len);
}

static int read_nonbloking(char *buf, size_t len)
{
	return serial_read_nonblocking(buf, len);
}

static int read_wait(size_t len)
{
	return serial_read_wait(len);
}

/***********************************************************************************************************************
* Function Name: write_data
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static void write_data(const char *buf, size_t len)
{
	int i;

	for ( i = 0; i < len; i++)
		outbyte(buf[i]);
}

/***********************************************************************************************************************
* Function Name: strequal
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static bool strequal(const char *str1, const char *str2)
{
	return !strcmp(str1, str2);
}

/***********************************************************************************************************************
* Function Name: u_to_string
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static ssize_t u_to_string(char *buf, size_t len, unsigned int value)
{
	return (ssize_t) snprintf(buf, len, "%u", value);
}

static ssize_t i_to_string(char *buf, size_t len, int value)
{
	return (ssize_t) snprintf(buf, len, "%d", value);
}
/***********************************************************************************************************************
* Function Name: dev_is_temp_module
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static bool dev_is_ad9361_module(const char *device)
{
	return strequal(device, "ad9361-phy")
			|| strequal(device, "cf-ad9361-lpc")
			|| strequal(device, "cf-ad9361-dds-core-lpc");
}

/***********************************************************************************************************************
* Function Name: read_value
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static long read_value(const char *str)
{
	char *end;
	int32_t value = strtol(str, &end, 0);

	if (end == str)
		return -EINVAL;
	else
		return value;
}


/***********************************************************************************************************************
* Function Name: read_value
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static unsigned long read_ul_value(const char *str)
{
	char *end;
	uint32_t value = strtoul(str, &end, 0);

	if (end == str)
		return -EINVAL;
	else
		return value;
}

/***********************************************************************************************************************
* Function Name: write_reg
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static int write_reg(unsigned int addr, uint32_t value)
{
	return -EPERM;
}

/***********************************************************************************************************************
* Function Name: read_reg
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static int read_reg(unsigned int addr, uint32_t *value)
{
	return 0;
}

extern const char *ad9361_ensm_states[12]; // todo add interface for it
extern int32_t ad9361_parse_fir(struct ad9361_rf_phy *phy,
	char *data, uint32_t size);
/***********************************************************************************************************************
* Function Name: read_attr
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static ssize_t read_attr(const char *device, const char *attr,
		char *buf, size_t len, bool debug)
{
	int ret = 0;
	unsigned long clk[6];

	if (!dev_is_ad9361_module(device))
		return -ENODEV;
	if(strequal(device, "ad9361-phy")) { // global attributes
		if(strequal(attr, "rx_path_rates")) {
			ad9361_get_trx_clock_chain(ad9361_phy, clk, NULL);
			ret = sprintf(buf, "BBPLL:%lu ADC:%lu R2:%lu R1:%lu RF:%lu RXSAMP:%lu",
					  clk[0], clk[1], clk[2], clk[3], clk[4], clk[5]);
			return ret;
		}
		if(strequal(attr, "tx_path_rates")) {
			ad9361_get_trx_clock_chain(ad9361_phy, NULL, clk);
			ret = sprintf(buf, "BBPLL:%lu DAC:%lu T2:%lu T1:%lu TF:%lu TXSAMP:%lu",
					  clk[0], clk[1], clk[2], clk[3], clk[4], clk[5]);
			return ret;
		}
		if(strequal(attr, "trx_rate_governor_available")) {
			ret = sprintf(buf, "%s", "nominal highest_osr");
			return ret;
		}
		if(strequal(attr, "trx_rate_governor")) {
			uint32_t rate_governor;
			ad9361_get_trx_rate_gov (ad9361_phy, &rate_governor);
			ret = sprintf(buf, "%s", rate_governor ? "nominal" : "highest_osr");
			return ret;
		}
		if(strequal(attr, "dcxo_tune_coarse_available")) {
			ret = sprintf(buf, "%s", ad9361_phy->pdata->use_extclk ? "[0 0 0]" : "[0 1 63]");
			return ret;
		}
		if (strequal(attr, "dcxo_tune_coarse")) {
			if (ad9361_phy->pdata->use_extclk)
				ret = -ENODEV;
			else
				ret = sprintf(buf, "%d", (int)ad9361_phy->pdata->dcxo_coarse);
			return ret;
		}
		if(strequal(attr, "dcxo_tune_fine_available")) {
			ret = sprintf(buf, "%s", ad9361_phy->pdata->use_extclk ? "[0 0 0]" : "[0 1 8191]");
			return ret;
		}
		if (strequal(attr, "dcxo_tune_fine")) {
			if (ad9361_phy->pdata->use_extclk)
				ret = -ENODEV;
			else
				ret = sprintf(buf, "%d", (int)ad9361_phy->pdata->dcxo_fine);
			return ret;
		}
		if (strequal(attr, "calib_mode_available")) {
			return (ssize_t) sprintf(buf, "auto manual tx_quad rf_dc_offs rssi_gain_step");
		}
		if (strequal(attr, "calib_mode")) {
			uint8_t en_dis;
			ad9361_get_tx_auto_cal_en_dis(ad9361_phy, &en_dis);
			return (ssize_t) snprintf(buf, len, "%s", en_dis ? "auto" : "manual");
		}
		if (strequal(attr, "xo_correction_available")) {
			//todo
			//clk[0] = clk_get_rate(ad9361_phy, ad9361_phy->ref_clk_scale[BB_REFCLK]);

		}
		if (strequal(attr, "ensm_mode_available")) {

			return (ssize_t) sprintf(buf, "%s", ad9361_phy->pdata->fdd ?
					"sleep wait alert fdd pinctrl pinctrl_fdd_indep" :
					"sleep wait alert rx tx pinctrl");
		}
		if (strequal(attr, "ensm_mode")) {
			ret = ad9361_ensm_get_state(ad9361_phy);
			if (ret < 0)
				return ret;
			if (ret >= ARRAY_SIZE(ad9361_ensm_states) ||
				ad9361_ensm_states[ret] == NULL) {
				return -EIO;
			}
			return sprintf(buf, "%s", ad9361_ensm_states[ret]);
		}
		if (strequal(attr, "filter_fir_config")) {
			return sprintf(buf, "FIR Rx: %d,%d Tx: %d,%d",
						ad9361_phy->rx_fir_ntaps, ad9361_phy->rx_fir_dec,
						ad9361_phy->tx_fir_ntaps, ad9361_phy->tx_fir_int);
		}
		return -ENOENT;
	}
	else if(strequal(device, "cf-ad9361-dds-core-lpc")) {

	}
	else if(strequal(device, "cf-ad9361-lpc")) {

	}
	return -ENODEV;
}

/***********************************************************************************************************************
* Function Name: write_attr
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static ssize_t write_attr(const char *device, const char *attr,
		const char *buf, size_t len, bool debug)
{
	u32 val = 0;
	int arg = -1, ret = 0;

	if (!dev_is_ad9361_module(device))
		return -ENODEV;
	if(strequal(device, "ad9361-phy")) {
		if(strequal(attr, "trx_rate_governor")) {
			if(strequal(buf, "nominal")) {
				ad9361_set_trx_rate_gov (ad9361_phy, 1);
			}
			else if(strequal(buf, "highest_osr")) {
				ad9361_set_trx_rate_gov (ad9361_phy, 0);
			}
			else
				ret =  -ENOENT;
			return ret;
		}
		if(strequal(attr, "dcxo_tune_coarse")) {
			uint32_t dcxo_coarse = read_ul_value(buf);
			dcxo_coarse = clamp_t(uint32_t, dcxo_coarse, 0, 63U);
			ad9361_phy->pdata->dcxo_coarse = dcxo_coarse;
			ret = ad9361_set_dcxo_tune(ad9361_phy, ad9361_phy->pdata->dcxo_coarse,
					ad9361_phy->pdata->dcxo_fine);
			return ret;
		}
		if(strequal(attr, "dcxo_tune_fine")) {
			uint32_t dcxo_fine = read_ul_value(buf);
			dcxo_fine = clamp_t(uint32_t, dcxo_fine, 0, 8191U);
			ad9361_phy->pdata->dcxo_fine = dcxo_fine;
			ret = ad9361_set_dcxo_tune(ad9361_phy, ad9361_phy->pdata->dcxo_coarse,
					ad9361_phy->pdata->dcxo_fine);
			return ret;
		}
		if (strequal(attr, "calib_mode")) {
			val = 0;
			if (strequal(buf, "auto")) {
				ad9361_set_tx_auto_cal_en_dis (ad9361_phy, 1);
			} else if (strequal(buf, "manual")) {
				ad9361_set_tx_auto_cal_en_dis (ad9361_phy, 0);
			}
			else if (!strncmp(buf, "tx_quad", 7)) {
				ret = sscanf(buf, "tx_quad %d", &arg);
				if (ret != 1)
					arg = -1;
				val = TX_QUAD_CAL;
			} else if (strequal(buf, "rf_dc_offs"))
				val = RFDC_CAL;
			else if (strequal(buf, "rssi_gain_step"))
				ret = ad9361_rssi_gain_step_calib(ad9361_phy);
			else
				return -ENOENT;

			if (val)
				ret = ad9361_do_calib(ad9361_phy, val, arg);

			return ret ? ret : len;
		}
		if (strequal(attr, "ensm_mode")) {
			bool res = false;
			ad9361_phy->pdata->fdd_independent_mode = false;

			if (strequal(buf, "tx"))
				val = ENSM_STATE_TX;
			else if (strequal(buf, "rx"))
				val = ENSM_STATE_RX;
			else if (strequal(buf, "alert"))
				val = ENSM_STATE_ALERT;
			else if (strequal(buf, "fdd"))
				val = ENSM_STATE_FDD;
			else if (strequal(buf, "wait"))
				val = ENSM_STATE_SLEEP_WAIT;
			else if (strequal(buf, "sleep"))
				val = ENSM_STATE_SLEEP;
			else if (strequal(buf, "pinctrl")) {
				res = true;
				val = ENSM_STATE_SLEEP_WAIT;
			} else if (strequal(buf, "pinctrl_fdd_indep")) {
				val = ENSM_STATE_FDD;
				ad9361_phy->pdata->fdd_independent_mode = true;
			} else
				return -ENOENT;

			ad9361_set_ensm_mode(ad9361_phy, ad9361_phy->pdata->fdd, res);
			ret = ad9361_ensm_set_state(ad9361_phy, val, res);
			return ret;
		}
		if(strequal(attr, "multichip_sync")) {
			uint32_t readin = read_ul_value(buf);
			if (ret < 0)
				return ret;
			ret = ad9361_mcs(ad9361_phy, readin);
			return ret;
		}
		if(strequal(attr, "rssi_gain_step_error")) {
			//todo
			return -ENOENT;
		}
		if(strequal(attr, "filter_fir_config")) {
			return ad9361_parse_fir(ad9361_phy, (char *)buf, len);
		}
		return -ENOENT;
	}
	else if(strequal(device, "cf-ad9361-dds-core-lpc")) {

	}
	else if(strequal(device, "cf-ad9361-lpc")) {

	}
	return -ENODEV;
}

/***********************************************************************************************************************
* Function Name: ch_read_attr
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static ssize_t ch_read_attr(const char *device, const char *channel,
		bool ch_out, const char *attr, char *buf, size_t len)
{
	int ret = 0;
	int32_t temp;
	if (!dev_is_ad9361_module(device))
		return -ENODEV;
	uint32_t ch_num = strequal(channel, "voltage0") ? 0 : 1;
	if(strequal(device, "ad9361-phy")) {
		if (strequal(attr, "sampling_frequency_available")) {
			return (ssize_t) snprintf(buf, len, "%d", 2);
		}
		if (strequal(attr, "sampling_frequency")) {
			uint32_t sampling_freq_hz;
			ad9361_get_rx_sampling_freq (ad9361_phy, &sampling_freq_hz);
			return (ssize_t) snprintf(buf, len, "%d", (int)sampling_freq_hz);
		}
		if (strequal(attr, "filter_fir_en")) {
			uint8_t en_dis;
			ad9361_get_rx_fir_en_dis (ad9361_phy, &en_dis);
			return (ssize_t) snprintf(buf, len, "%d", en_dis);
		}
		if (strequal(attr, "rssi")) {
			if(ch_out) {
				uint32_t rssi_db_x_1000;
				ad9361_get_tx_rssi(ad9361_phy, ch_num, &rssi_db_x_1000);
				return ret < 0 ? ret : sprintf(buf, "%lu.%02lu dB",
						rssi_db_x_1000 / 1000, rssi_db_x_1000 % 1000);
			}
			else {
				struct rf_rssi rssi = {0};
				ret = ad9361_get_rx_rssi (ad9361_phy, ch_num, &rssi);
				return ret < 0 ? ret : sprintf(buf, "%lu.%02lu dB",
						rssi.symbol / rssi.multiplier, rssi.symbol % rssi.multiplier);
			}
		}
		if (strequal(attr, "rf_bandwidth")) {
			if(ch_out) {
				return sprintf(buf, "%lu", ad9361_phy->current_tx_bw_Hz);
			}
			else {
				return sprintf(buf, "%lu", ad9361_phy->current_rx_bw_Hz);
			}
		}
		if (strequal(attr, "rf_bandwidth_available")) {
			if(ch_out) {
				return sprintf(buf, "[200000 1 40000000]");
			}
			else {
				return sprintf(buf, "[200000 1 56000000]");
			}
		}
		if (strequal(attr, "rf_port_select_available")) {
			if(ch_out) {
				return (ssize_t) sprintf(buf, "%s %s",
						ad9361_rf_tx_port[0],
						ad9361_rf_tx_port[1]);
			}
			else {
				ssize_t bytes_no = 0;
				for(int i = 0; i < sizeof(ad9361_rf_rx_port) / sizeof(ad9361_rf_rx_port[0]); i++) {
					if(i > 0 ) {
						bytes_no += sprintf(buf + bytes_no, " ");
					}
					bytes_no += sprintf(buf + bytes_no, "%s", ad9361_rf_rx_port[i]);
					if(bytes_no < 0) {
						break;
					}
				}
				return bytes_no;
			}
		}
		if (strequal(attr, "rf_port_select")) {
			if(ch_out) {
				uint32_t mode;
				ret = ad9361_get_tx_rf_port_output(ad9361_phy, &mode);
				return ret < 0 ? ret : sprintf(buf, "%s", ad9361_rf_tx_port[mode]);
			}
			else {
				uint32_t mode;
				ret = ad9361_get_rx_rf_port_input(ad9361_phy, &mode);
				return ret < 0 ? ret : sprintf(buf, "%s", ad9361_rf_rx_port[mode]);
			}
		}
		if (strequal(attr, "gain_control_mode_available")) {
			return (ssize_t) sprintf(buf, "%s %s %s %s",
					ad9361_agc_modes[0],
					ad9361_agc_modes[1],
					ad9361_agc_modes[2],
					ad9361_agc_modes[3]);
		}
		if (strequal(attr, "gain_control_mode")) {
			return (ssize_t) sprintf(buf, "%s", ad9361_agc_modes[ad9361_phy->agc_mode[ch_num]]);
		}
		if (strequal(attr, "hardwaregain_available")) {
			if (ch_out) {
				return (ssize_t) snprintf(buf, len, "[%d, %d, %d]", 0, 250, 89750);
			} else {
				return (ssize_t) snprintf(buf, len, "[%ld, %d, %ld]",
						ad9361_phy->rx_gain[ad9361_phy->current_table].starting_gain_db,
						1,
						ad9361_phy->rx_gain[ad9361_phy->current_table].max_gain_db);
			}
		}
		if (strequal(attr, "hardwaregain")) {
			if(ch_out) {
				int32_t ret = ad9361_get_tx_atten(ad9361_phy, ch_num + 1);
				if (ret < 0) {
					return -EINVAL;
				}
				int32_t val1 = -1 * (ret / 1000);
				int32_t val2 = (ret % 1000) * 1000;
				if (!val1) {
					val2 *= -1;

				}
				int i = 0;
				if(val2 < 0 && val1 >= 0) {
					ret = (ssize_t) snprintf(buf, len, "-");
					i++;
				}
				ret = i + (ssize_t) snprintf(&buf[i], len, "%ld.%.6ld dB", val1, labs(val2));
				return ret;
			} else {
				struct rf_rx_gain rx_gain = {0};
				int32_t ret = ad9361_get_rx_gain(ad9361_phy, ad9361_1rx1tx_channel_map(ad9361_phy,
						false, ch_num + 1), &rx_gain);
				if (ret < 0) {
					return -EINVAL;
				}
				return (ssize_t) snprintf(buf, len, "%d.000000 dB", (int)rx_gain.gain_db);
			}

		}
		if(strequal(channel, "temp0")) {
			if(strequal(attr, "input")) {
				ad9361_get_temperature(ad9361_phy, &temp);
					return (ssize_t) snprintf(buf, len, "%d", (int)temp);
			}
		}
	}
	else if(strequal(device, "cf-ad9361-dds-core-lpc")) {

	}
	else if(strequal(device, "cf-ad9361-lpc")) {

	}
	return -ENOENT;
}

/***********************************************************************************************************************
* Function Name: ch_write_attr
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static ssize_t ch_write_attr(const char *device, const char *channel,
		bool ch_out, const char *attr, const char *buf, size_t len)
{
	int ret;
	if (!dev_is_ad9361_module(device))
			return -ENODEV;
	uint32_t ch_num = strequal(channel, "voltage0") ? 0 : 1;
	if (strequal(attr, "sampling_frequency")) {
		uint32_t sampling_freq_hz = read_ul_value(buf);
		ad9361_set_rx_sampling_freq (ad9361_phy, sampling_freq_hz);
			return len;
	}
	if (strequal(attr, "filter_fir_en")) {
		uint8_t en_dis = read_ul_value(buf);
		ad9361_set_rx_fir_en_dis (ad9361_phy, en_dis);
		return len;
	}
	if (strequal(attr, "rf_port_select")) {
		uint32_t i = 0;
		if(ch_out) {
			for(i = 0; i < sizeof(ad9361_rf_tx_port) / sizeof(ad9361_rf_tx_port[0]); i++) {
				if(strequal(ad9361_rf_tx_port[i], buf)) {
					break;
				}
			}
			if(i >= sizeof(ad9361_rf_tx_port) / sizeof(ad9361_rf_tx_port[0])) {
				return -EINVAL;
			}
			ret = ad9361_set_tx_rf_port_output(ad9361_phy, i);
			return ret < 0 ? ret : len;
		}
		else {
			for(i = 0; i < sizeof(ad9361_rf_rx_port) / sizeof(ad9361_rf_rx_port[0]); i++) {
				if(strequal(ad9361_rf_rx_port[i], buf)) {
					break;
				}
			}
			if(i >= sizeof(ad9361_rf_tx_port) / sizeof(ad9361_rf_tx_port[0])) {
				return -EINVAL;
			}
			ret = ad9361_set_rx_rf_port_input(ad9361_phy, i);
			return ret < 0 ? ret : len;
		}
	}
	if (strequal(attr, "gain_control_mode")) {
		struct rf_gain_ctrl gc = {0};
		int i;
		for(i = 0; i < sizeof(ad9361_agc_modes) / sizeof(ad9361_agc_modes[0]); i++) {
			if(strequal(ad9361_agc_modes[i], buf)) {
				break;
			}
		}
		if(i >= sizeof(ad9361_agc_modes) / sizeof(ad9361_agc_modes[0])) {
			return -EINVAL;
		}
		uint32_t mode = i;
		if (ad9361_phy->agc_mode[ch_num] == mode)
			return len;
		gc.ant = ad9361_1rx1tx_channel_map(ad9361_phy, false, ch_num + 1);
		gc.mode = ad9361_phy->agc_mode[ch_num] = mode;
		ad9361_set_gain_ctrl_mode(ad9361_phy, &gc);
		return len;
	}
	if (strequal(attr, "rf_bandwidth")) {
		uint32_t rf_bandwidth = read_ul_value(buf);
		rf_bandwidth = ad9361_validate_rf_bw(ad9361_phy, rf_bandwidth);
		if(ch_out) {
			if(ad9361_phy->current_tx_bw_Hz != rf_bandwidth) {
				ret = ad9361_update_rf_bandwidth(ad9361_phy, ad9361_phy->current_rx_bw_Hz, rf_bandwidth);
			}
		}
		else {
			if(ad9361_phy->current_rx_bw_Hz != rf_bandwidth) {
				ret = ad9361_update_rf_bandwidth(ad9361_phy, rf_bandwidth, ad9361_phy->current_tx_bw_Hz);
			}
		}
		return len;
	}
	if (strequal(attr, "hardwaregain")) {
		float gain = strtof(buf, NULL);
		int32_t val1 = (int32_t)gain;
		int32_t val2 = (int32_t)(gain * 1000) % 1000;
		if (ch_out) {
			int ch;
			if (val1 > 0 || (val1 == 0 && val2 > 0)) {
				return -EINVAL;
			}
			uint32_t code = ((abs(val1) * 1000) + (abs(val2)/* / 1000*/));
			ch = ad9361_1rx1tx_channel_map(ad9361_phy, true, ch_num);
			ret = ad9361_set_tx_atten(ad9361_phy, code, ch == 0, ch == 1,
					!ad9361_phy->pdata->update_tx_gain_via_alert);
			if (ret < 0) {
				return -EINVAL;
			}
		} else {
			struct rf_rx_gain rx_gain = {0};
			rx_gain.gain_db = val1;
			ret = ad9361_set_rx_gain(ad9361_phy,
					ad9361_1rx1tx_channel_map(ad9361_phy, false, ch_num + 1), &rx_gain);
			if (ret < 0) {
				return -EINVAL;
			}
		}
		return len;
	}




	return -ENOENT;
}

/***********************************************************************************************************************
* Function Name: open_dev
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static int open_dev(const char *device, size_t sample_size, uint32_t mask)
{
	if (!dev_is_ad9361_module(device))
		return -ENODEV;

	if (mask & ~input_channel_mask)
		return -ENOENT;

	request_mask = mask;
	return 0;
}



/***********************************************************************************************************************
* Function Name: close_dev
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static int close_dev(const char *device)
{
	return dev_is_ad9361_module(device) ? 0 : -ENODEV;
}

/***********************************************************************************************************************
* Function Name: r_uart1_callback_receiveend
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static int get_mask(const char *device, uint32_t *mask)
{
	if (!dev_is_ad9361_module(device))
		return -ENODEV;

	//*mask = request_mask;
	*mask = input_channel_mask; // this way client has to do demux of data
	return 0;
}

static ssize_t write_dev(const char *device, const char *buf, size_t bytes_count)
{
	//todo
	dac_write_buffer(ad9361_phy, 0, 0);
	return bytes_count;
}
/***********************************************************************************************************************
* Function Name: read_dev
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
static ssize_t read_dev(const char *device, char *buf, size_t bytes_count)
{
	int i, sampleSize;

	if (!dev_is_ad9361_module(device))
			return -ENODEV;

	if(adc_st.rx2tx2)
		{
			sampleSize = bytes_count / 8;
		}
		else
		{
			sampleSize = bytes_count / 4;
		}

	adc_capture(sampleSize, ADC_DDR_BASEADDR);
	Xil_DCacheInvalidateRange(ADC_DDR_BASEADDR,	bytes_count);

	for ( i = 0; i < bytes_count; i++)
		buf[i] = Xil_In8(ADC_DDR_BASEADDR + i);

	return bytes_count;
}

/***********************************************************************************************************************
* Name: tinyiiod_ops
* Description  : None
* Arguments    : None
* Return Value : None
***********************************************************************************************************************/
const struct tinyiiod_ops ops = {
	//communication
	.read = read,
	.read_line = read_line,
	.read_nonbloking = read_nonbloking,
	.read_wait = read_wait,
	.write = write_data,

	//device operations
	.read_attr = read_attr,
	.write_attr = write_attr,
	.ch_read_attr = ch_read_attr,
	.ch_write_attr = ch_write_attr,
	.read_device = read_dev,
	.write_device = write_dev,

	//
	.open = open_dev,
	.close = close_dev,
	.get_mask = get_mask,
};