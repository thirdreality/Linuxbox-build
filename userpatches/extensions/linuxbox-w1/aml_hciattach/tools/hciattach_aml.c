/*
 *
 *  Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 *  Not a Contribution.
 *
 *  Copyright 2012 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you
 *  may not use this file except in compliance with the License. You may
 *  obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 *  implied. See the License for the specific language governing
 *  permissions and limitations under the License.
 *
 */

/******************************************************************************
 *
 *  Filename:      hciattach_aml.c
 *
 *  Description:   Contains controller-specific functions, like
 *                      firmware patch download
 *                      low power mode operations
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <bluetooth/bluetooth.h>
#include "hciattach_aml.h"
#include "hciattach.h"

/******************************************************************************
**  Macro Definition
******************************************************************************/
#define pr_info(fmt, args...) fprintf(stdout,"%s:" fmt "\n", __func__, ##args)
#define pr_err(fmt, args...) fprintf(stdout,"%s:[%d] " fmt "\n", __func__, __LINE__, ##args)

/*mac addr len */
#define MAC_LEN 6
#define MAC_DELAY 600
uint8_t vendor_local_addr[MAC_LEN];
#define SAVE_MAC "/etc/bluetooth/aml/bt_mac"

/*fwICCM is 0 to download*/
#define fwICCM 0
/*fwDCCM is 1 to download*/
#define fwDCCM 1

#define CHECK_FW 0

#define HCI_EVT_CMD_CMPL_LOCAL_NAME_STRING      6

#define UINT8_TO_STREAM(p, u8)   { *(p)++ = (uint8_t)(u8); }
#define UINT16_TO_STREAM(p, u16) { *(p)++ = (uint8_t)(u16); *(p)++ = (uint8_t)((u16) >> 8); }
#define UINT32_TO_STREAM(p, u32) { *(p)++ = (uint8_t)(u32); *(p)++ = (uint8_t)((u32) >> 8); *(p)++ = (uint8_t)((u32) >> 16); *(p)++ = (uint8_t)((u32) >> 24); }

#define ICCM_RAM_BASE           (0x000000)
#define DCCM_RAM_BASE           (0xd00000)
#define RW_OPERATION_SIZE       (248)
#define TCI_READ_REG				0xfef0
#define TCI_WRITE_REG				0xfef1
#define TCI_UPDATE_UART_BAUDRATE	0xfef2
#define TCI_DOWNLOAD_BT_FW			0xfef3
#define HCI_VSC_WRITE_BD_ADDR       0xFC1A
#define ICCM_DCCM_MAX               (32 * 1024 * 1024)

/*Current module*/
static int AML_MODULE = W1_UART;

#define USB_POWER_UP        _IO('m', 1)
#define USB_POWER_DOWN      _IO('m', 2)
#define SDIO_POWER_UP       _IO('m', 3)
#define SDIO_POWER_DOWN     _IO('m', 4)
#define SDIO_GET_DEV_TYPE   _IO('m', 5)
#define GET_AML_WIFI_MODULE _IO('m', 7)

#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))

/******************************************************************************
**  Variables
******************************************************************************/

#if 0
unsigned int fwICCM_len = 0,
unsigned int fwICCM_offset = 0;

unsigned int fwDCCM_len = 0,
unsigned int fwDCCM_offset = 0;
#endif

static vnd_userial_cb_t vnd_userial;
static int antenna_number = 2;
static int load_efuse = 0;

/******************************************************************************
**  Extern variables
******************************************************************************/
//extern unsigned char vnd_local_bd_addr[6];
static const vnd_module_t aml_module[] ={
	{W1_UART,  256, "W1_UART",  "aml_w1",    AML_W1_BT_FW_UART_FILE },
	{W1U_UART, 256, "W1U_UART", "aml_w1u_s", AML_W1U_BT_FW_UART_FILE},
	{W2_UART,  256, "W2_UART",  "aml_w2_s",  AML_W2_BT_FW_UART_FILE },
	{W2L_UART, 384, "W2L_UART", "aml_w2l_s", AML_W2L_BT_FW_UART_FILE},
};

static const vnd_chip_t aml_chip[] ={
	{W1_UART,     "0x8888"},
	{W1U_UART,    "0x0540"},
	{W2_UART,     "0x0640"},
	{W2_UART,     "0x0642"},  // pcie module
	{W2L_UART,    "0x0800"},
	{W2L_UART,    "0x0840"},
};

typedef int (*callback)(int);

static int do_write(int fd, unsigned char *buf, int len)
{
	int ret = 0;
	int write_offset = 0;
	int write_len = len;

	do {
		ret = write(fd, buf + write_offset, write_len);
		if (ret < 0) {
			pr_info("write fail ret:%d,err:%s", ret, strerror(errno));
			return ret;
		} else if (ret == 0) {
			pr_info("write fail with ret:0,err:%s", strerror(errno));
			return ret;
		} else {
			if (ret < write_len) {
				pr_info("write pending,do write ret:%d,err:%s", ret, strerror(errno));
				write_len = write_len - ret;
				write_offset = ret;
			} else {
				pr_info("write success");
				break;
			}
		}
	} while (1);

	return len;
}

static int aml_hci_send_cmd(int fd, unsigned char *cmd, int cmd_size, unsigned char *rsp,
	unsigned int rsp_size)
{
	int ret = -1;

	ret = do_write(fd, cmd, cmd_size);
	if (ret != cmd_size) {
		pr_info("send fail,ret:%d", ret);
		goto exit;
	}

	memset(rsp, 0, rsp_size);

	/* Wait for command complete event */
	ret = read_hci_event(fd, rsp, rsp_size);
	if (ret < 0) {
		pr_info("read fail,ret:%d", ret);
		goto exit;
	}

exit:
	return ret;
}

static int aml_set_wake_manf_data(int fd)
{
	int ret = 0;
	unsigned char rsp[HCI_MAX_EVENT_SIZE] = {'0'};
	unsigned char APCF_config_manf_data[] =
		{0x01, 0x22, 0xFC, 0x05, 0x19, 0xff, 0x01, 0x0a, 0xb};

	if (AML_MODULE != W1_UART) {
		pr_info("set wake APCF_config_manf_data");
		ret = aml_hci_send_cmd(fd, (unsigned char *)APCF_config_manf_data,
			sizeof(APCF_config_manf_data),(unsigned char *)rsp, HCI_MAX_EVENT_SIZE);
	}

	return ret;
}

static int aml_woble_configure(int fd)
{
	unsigned char rsp[HCI_MAX_EVENT_SIZE] = {'0'};
	unsigned char reset_cmd[] = {0x01, 0x03, 0x0C, 0x00};
	unsigned char read_BD_ADDR[] = {0x01, 0x09, 0x10, 0x00};
	unsigned char APCF_config_manf_data[] =
		{0x01, 0x22, 0xFC, 0x05, 0x19, 0xff, 0x01, 0x0a, 0xb};
	unsigned char APCF_enable[] = {0x01, 0x57, 0xFD, 0x02, 0x00, 0x01};
	unsigned char le_set_evt_mask[] =
		{0x01, 0x01, 0x20, 0x08, 0x7F, 0x1A, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
	unsigned char le_scan_param_setting[] =
		{0x01, 0x0b, 0x20, 0x07, 0x00, 0x10, 0x00, 0x10, 0x00, 0x00, 0x00};
	unsigned char le_scan_enable[] = {0x01, 0x0c, 0x20, 0x02, 0x01, 0x00};
	unsigned char host_sleep_VSC[] = {0x01, 0x21, 0xfc, 0x01, 0x01};

	if (AML_MODULE == W1_UART) {
		pr_info("W1_UART set aml_woble_cfg");
		aml_hci_send_cmd(fd, (unsigned char *)reset_cmd, sizeof(reset_cmd),
			(unsigned char *)rsp, HCI_MAX_EVENT_SIZE);
		aml_hci_send_cmd(fd, (unsigned char *)host_sleep_VSC, sizeof(host_sleep_VSC),
			(unsigned char *)rsp, HCI_MAX_EVENT_SIZE);
		aml_hci_send_cmd(fd, (unsigned char *)APCF_config_manf_data,
			sizeof(APCF_config_manf_data),(unsigned char *)rsp, HCI_MAX_EVENT_SIZE);
		aml_hci_send_cmd(fd, (unsigned char *)le_set_evt_mask,
			sizeof(le_set_evt_mask), (unsigned char *)rsp, HCI_MAX_EVENT_SIZE);
		aml_hci_send_cmd(fd, (unsigned char *)le_scan_param_setting,
			sizeof(le_scan_param_setting), (unsigned char *)rsp, HCI_MAX_EVENT_SIZE);
		aml_hci_send_cmd(fd, (unsigned char *)le_scan_enable,
			sizeof(le_scan_enable), (unsigned char *)rsp, HCI_MAX_EVENT_SIZE);
	} else {
		pr_info("other amlbt set aml_woble_cfg");
		aml_hci_send_cmd(fd, (unsigned char *)reset_cmd, sizeof(reset_cmd),
			(unsigned char *)rsp, HCI_MAX_EVENT_SIZE);
	}

	return 0;
}

static int antenna_number_act(const char * p_name, char * p_value)
{
	antenna_number = strtol(p_value, NULL, 10);
	if (antenna_number == 0) {
		pr_info("antenna_number use default value 2");
		antenna_number = 2;
	}
	pr_info("%s = %d", p_name, antenna_number);
}

static int load_efuse_act(const char * p_name, char* p_value)
{
	load_efuse = strtol(p_value, NULL, 10);
	pr_info("%s = %d", p_name, load_efuse);
}

static const d_entry_t entry_table[] = {
	{"antenna_number", antenna_number_act},
	{"load_efuse", load_efuse_act},
	{NULL, NULL}
};

/*****************************************************************************
**   Functions
*****************************************************************************/

/*******************************************************************************
**
** Function        aml_userial_to_tcio_baud
**
** Description     helper function converts USERIAL baud rates into TCIO
**                  conforming baud rates
**
** Returns         TRUE/FALSE
**
*******************************************************************************/
static unsigned char aml_userial_to_tcio_baud(unsigned char cfg_baud, unsigned int *baud)
{
	if (cfg_baud == USERIAL_BAUD_115200)
		*baud = B115200;
	else if (cfg_baud == USERIAL_BAUD_4M)
		*baud = B4000000;
	else if (cfg_baud == USERIAL_BAUD_3M)
		*baud = B3000000;
	else if (cfg_baud == USERIAL_BAUD_2M)
		*baud = B2000000;
	else if (cfg_baud == USERIAL_BAUD_1M)
		*baud = B1000000;
	else if (cfg_baud == USERIAL_BAUD_921600)
		*baud = B921600;
	else if (cfg_baud == USERIAL_BAUD_460800)
		*baud = B460800;
	else if (cfg_baud == USERIAL_BAUD_230400)
		*baud = B230400;
	else if (cfg_baud == USERIAL_BAUD_57600)
		*baud = B57600;
	else if (cfg_baud == USERIAL_BAUD_19200)
		*baud = B19200;
	else if (cfg_baud == USERIAL_BAUD_9600)
		*baud = B9600;
	else if (cfg_baud == USERIAL_BAUD_1200)
		*baud = B1200;
	else if (cfg_baud == USERIAL_BAUD_600)
		*baud = B600;
	else
	{
		pr_err("userial vendor open: unsupported baud idx %i", cfg_baud);
		*baud = B115200;
		return FALSE;
	}

	return TRUE;
}

static int get_config(const char* file)
{
	FILE * config_fd;
	char line[MAX_LINE_LEN +1] = {0};
	char *p_name = NULL;
	char *p_value = NULL;
	d_entry_t * temp_table = NULL;

	config_fd = fopen(file, "r");
	if (config_fd == NULL) {
		pr_err("open file:%s fail", file);
		return 0;
	}

	while (fgets(line, MAX_LINE_LEN +1, config_fd) != NULL) {
		if (line[0] == '#') {
			continue;
		}

		p_name = strtok(line, DELIM);
		if (p_name == NULL) {
			continue;
		}

		p_value = strtok(NULL, DELIM);

		temp_table = (d_entry_t*)entry_table;
		while (temp_table->entry_name != NULL) {
			if (!strcmp(temp_table->entry_name, p_name)) {
				temp_table->p_action(temp_table->entry_name, p_value);
				break;
			}
			temp_table ++;
		}
	}
	fclose(config_fd);
	return 1;
}

/******************************************************************************
**  delay function
******************************************************************************/
static void ms_delay(uint32_t timeout)
{
	struct timespec delay;
	int err;

	if (timeout == 0)
		return;

	delay.tv_sec = timeout / 1000;
	delay.tv_nsec = 1000 * 1000 * (timeout % 1000);

	/* [u]sleep can't be used because it uses SIGALRM */
	do {
		err = nanosleep(&delay, &delay);
	} while (err < 0 && errno == EINTR);
}

/******************************************************************************
** save file
******************************************************************************/
static uint8_t * aml_getprop_read(const char* str)
{
	int fd, n;
	char buf[18];
	memset(buf, '\0', sizeof(buf));
	fd = open(str, O_RDONLY|O_CREAT, 0666);
	if (fd < 0)
	{
		perror("open SAVE_MAC read");
		goto error;
	}
	n = read(fd, buf, sizeof(buf)-1);
	if (n < sizeof(buf)-1)
	{
		pr_info("n < sizeof(buf)");
		close(fd);
		goto error;
	}

	buf[sizeof(buf)-1] ='\0';
	close(fd);

	if (strnlen(buf, 17) != 17)
	{
		pr_info("don't matching bt mac");
		goto error;
	}
	sscanf(buf, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                 &vendor_local_addr[0], &vendor_local_addr[1], &vendor_local_addr[2],\
                 &vendor_local_addr[3], &vendor_local_addr[4], &vendor_local_addr[5]);
	return vendor_local_addr;

error:
	return NULL;

}

static int aml_setprop_write(const char *str, int size)
{
	int err = -1;
	int fd;
	fd = open(SAVE_MAC, O_WRONLY|O_CREAT, 0666);
	if (fd < 0)
	{
		perror("open SAVE_MAC write");
		goto error;
	}
	err = write(fd, str, size);
	if (err != size)
	{
		pr_err("write fail");
	}
	close(fd);

error:
	return err;

}

static int get_fw_version(char *str)
{
	int fd;
	int ret;
	char * fw_version = NULL;
	str = str + 7; //skip 7byte

	ret = asprintf(&fw_version, "fw_version: date = %02x.%02x, number = 0x%02x%02x\n", *(str+1),*str,*(str+3),*(str+2));
	if (ret <= 0)
	{
		goto error;
	}
	fd = open(FW_VER_FILE,  O_WRONLY|O_CREAT|O_TRUNC, 0666);
	if (fd < 0)
	{
		pr_err("open fw_file fail");
		free(fw_version);
		goto error;
	}

	(void)write(fd, fw_version, strlen(fw_version));

	free(fw_version);
	close(fd);
error:
	return 0;
}

/******************************************************************************
**  set bdaddr
******************************************************************************/
static int aml_set_bdaddr(int fd)
{
	int size;
	int err = -1;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	char *cmd_hdr = NULL;
	char buf[18];
	uint8_t *tempbuf;

	uint8_t local_addr[MAC_LEN];
	if ((tempbuf = aml_getprop_read(NUIFYKEY_MAC)) != NULL)
	{
		memcpy(local_addr, tempbuf, MAC_LEN);
		goto set_mac;
	}
	else if ((tempbuf = aml_getprop_read(SAVE_MAC)) != NULL)
	{
		memcpy(local_addr, tempbuf, MAC_LEN);
		goto set_mac;
	}

	memset(buf, '\0', sizeof(buf));
	srand(time(NULL));
	memset(local_addr, '\0', MAC_LEN);

	local_addr[0] = 0x22;
	local_addr[1] = 0x22;
	local_addr[2] = (uint8_t)rand();
	local_addr[3] = (uint8_t)rand();
	local_addr[4] = (uint8_t)rand();
	local_addr[5] = (uint8_t)rand();

	sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x",local_addr[0],\
					 local_addr[1], local_addr[2], local_addr[3], local_addr[4], local_addr[5]);
	err = aml_setprop_write(buf, sizeof(buf));
	if (err < 0)
	{
		pr_info("aml_getprop_write fail");
	}

set_mac:
	pr_info("set bdaddr: %02x:%02x:%02x:%02x:%02x:%02x", local_addr[0],\
					 local_addr[1], local_addr[2], local_addr[3], local_addr[4], local_addr[5]);
	memset(cmd, 0x0, HCI_MAX_CMD_SIZE);
	memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

	cmd_hdr = (void *) (cmd + 1);
	cmd[0]	= HCI_COMMAND_PKT;
	UINT16_TO_STREAM(cmd_hdr, HCI_VSC_WRITE_BD_ADDR);
	*cmd_hdr++ = MAC_LEN;
	*cmd_hdr++ = local_addr[5];
	*cmd_hdr++ = local_addr[4];
	*cmd_hdr++ = local_addr[3];
	*cmd_hdr++ = local_addr[2];
	*cmd_hdr++ = local_addr[1];
	*cmd_hdr++ = local_addr[0];

	size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + MAC_LEN);

	err = write(fd, cmd, size);
	if (err != size) {
		pr_err("Send failed with ret value: %d", err);
		goto error;
	}

	/* Wait for command complete event */
	err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
	if ( err < 0) {
		pr_err("Failed to set patch info on Controllern");
		goto error;
	}

	if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
		pr_err("Failed to set_bdaddr, command failure");
		return -1;
	}

	get_fw_version(rsp);
	pr_info("success");

error:
	return err;

}

/*******************************************************************************
**
** Function         hw_config_set_rf_params
**
** Description      Config rf parameters to controller
**
** Returns
**
**
*******************************************************************************/
static int hw_config_set_rf_params(int fd)
{
	int retval = FALSE;
	int err = -1;
	int size;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	char *cmd_hdr = NULL;
	uint8_t antenna_num = 0;
	int fd_a2dp_cfg = 0;
	char buffer[255] = { 0 };
	char c = '=';
	uint32_t reg_data = 0;
	uint8_t a2dp_sink_enable = 0;

	antenna_num = antenna_number;

	pr_info("Setting parameters to controller: antenna number=%d.", antenna_num);

	//////////////////////////////////////////////////////////////////
	fd_a2dp_cfg = open(AML_A2DP_CFG_FILE, O_RDONLY);
	if (fd_a2dp_cfg < 0)
	{
		pr_info("Open failed:%s", strerror(errno));
		return FALSE;
	}

	size = read(fd_a2dp_cfg, buffer, sizeof(buffer));
	if (size < 0)
	{
		pr_info("Read failed:%s", strerror(errno));
		close(fd_a2dp_cfg);
		return FALSE;
	}
	buffer[sizeof(buffer) - 1] = 0;
	char *ptr_a2dp_cfg = strchr(buffer, c);
	if (!ptr_a2dp_cfg) {
		pr_info("wrong a2dp_sink_enable value");
		close(fd_a2dp_cfg);
		return FALSE;
	}
	ptr_a2dp_cfg++;
	a2dp_sink_enable = atoi(ptr_a2dp_cfg);

	close(fd_a2dp_cfg);

	pr_info("Setting parameters to controller: a2dp_sink_enable=%d.", a2dp_sink_enable);

	memset(cmd, 0x0, HCI_MAX_CMD_SIZE);
	memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

	cmd_hdr = (void *) (cmd + 1);
	cmd[0]	= HCI_COMMAND_PKT;
	UINT16_TO_STREAM(cmd_hdr, TCI_WRITE_REG);

	*cmd_hdr++ = 0x08;       	/* parameter length */
	UINT32_TO_STREAM(cmd_hdr, 0xf03040);  /* addr */
	if (antenna_num == 1)
	{
		// UINT32_TO_STREAM(p, 0x10000000);
		reg_data = 0x10000000;
	}
	else if (antenna_num == 2)
	{
		//UINT32_TO_STREAM(p, 0x20000000);
		reg_data = 0x20000000;
	}

	if (a2dp_sink_enable == 1)
	{
		reg_data |= (1<<25);	// bit25 means a2dp_sink_enable.
	}

	UINT32_TO_STREAM(cmd_hdr, reg_data);

	size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + 0x08);

	err = write(fd, cmd, size);
	if (err != size) {
		pr_err("Send failed with ret value: %d", err);
		goto error;
	}

	/* Wait for command complete event */
	err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
	if ( err < 0) {
		pr_err("Failed to set rf params");
		goto error;
	}

error:
	return err;
}

static int aml_start_cpu_uart(int fd, callback func)
{
	int size;
	int err = -1;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	char *cmd_hdr = NULL;

	memset(cmd, 0x0, HCI_MAX_CMD_SIZE);
	memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

	cmd_hdr = (void *) (cmd + 1);
	cmd[0]	= HCI_COMMAND_PKT;
	UINT16_TO_STREAM(cmd_hdr, TCI_WRITE_REG);
	*cmd_hdr++ = 0x08;
	UINT32_TO_STREAM(cmd_hdr, 0xf03058);
	UINT32_TO_STREAM(cmd_hdr, 0x700);

	/* Total length of the packet to be sent to the Controller */
	size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + 0x08);

	/* Send the HCI command packet to UART for transmission */
	pr_info("HCI CMD: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",cmd[0], cmd[1], cmd[2], cmd[3],\
					 cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9], cmd[10], cmd[11]) ;
	err = write(fd, cmd, size);
	if (err != size) {
		pr_err("Send failed with ret value: %d", err);
		goto error;
	}

	/* Wait for command complete event */
	err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
	if ( err < 0) {
		pr_err("Failed to set patch info on Controllern");
		goto error;
	}

	if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
		pr_err("Failed to cpu_uart, command failure");
		return -1;
	}
	pr_info("success");
	ms_delay(MAC_DELAY);
	pr_info("delay %d",MAC_DELAY);
	if (!load_efuse && func != NULL)
	{
		err = func(fd);
		if (err < 0)
		{
			pr_err("cmd send fail");
			goto error;
		}
	}
	else
	{
		pr_info("func is null");
	}

error:
	return err;

}

/******************************************************************************
**  start cup cmd
******************************************************************************/
static int aml_start_cpu_before_cmd(int fd)
{
	int size;
	int err = -1;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	char *cmd_hdr = NULL;

	memset(cmd, 0x0, HCI_MAX_CMD_SIZE);
	memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

	cmd_hdr = (void *) (cmd + 1);
	cmd[0]	= HCI_COMMAND_PKT;
	UINT16_TO_STREAM(cmd_hdr, TCI_WRITE_REG);
	*cmd_hdr++ = 0x08;
	UINT32_TO_STREAM(cmd_hdr, 0xa7000c);
	UINT32_TO_STREAM(cmd_hdr, 0x8000000);

	/* Total length of the packet to be sent to the Controller */
	size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + 0x08);

	/* Send the HCI command packet to UART for transmission */
	pr_info("HCI CMD: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",cmd[0], cmd[1], cmd[2], cmd[3],\
					 cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9], cmd[10], cmd[11]) ;
	err = write(fd, cmd, size);
	if (err != size) {
		pr_err("Send failed with ret value: %d", err);
		goto error;
	}

	/* Wait for command complete event */
	err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
	if ( err < 0) {
		pr_err("Failed to set patch info on Controllern");
		goto error;
	}

	if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
		pr_err("Failed to cpu_before_cmd, command failure");
		return -1;
	}
	pr_info("success");
	err = aml_start_cpu_uart(fd, aml_set_bdaddr); //aml_hci_reset);
	if (err < 0)
	{
		pr_err("aml_start_cpu_uart cmd load fail");
		goto error;
	}

error:
	return err;

}

/******************************************************************************
**  disable download
******************************************************************************/

static int aml_disable_event(int fd)
{
	int size;
	int err = -1;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	hci_command_hdr *cmd_hdr;

	memset(cmd, 0x0, HCI_MAX_CMD_SIZE);
	memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

	cmd_hdr = (void *) (cmd + 1);
	cmd[0]	= HCI_COMMAND_PKT;
	cmd_hdr->opcode = TCI_WRITE_REG;
	cmd_hdr->plen	= 0x08;
	cmd[4] = 0x14;
	cmd[5] = 0x00;
	cmd[6] = 0xa7;
	cmd[7] = 0x00;
	cmd[8] = 0x00;
	cmd[9] = 0x00;
	cmd[10]= 0x00;
	cmd[11]= 0x00;


	/* Total length of the packet to be sent to the Controller */
	size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd_hdr->plen);

	/* Send the HCI command packet to UART for transmission */
	pr_info("HCI CMD: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",cmd[0], cmd[1], cmd[2], cmd[3],\
					 cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9], cmd[10], cmd[11]) ;
	err = write(fd, cmd, size);
	if (err != size) {
		pr_err("Send failed with ret value: %d", err);
		goto error;
	}
	pr_info("Received HCI-Vendor Specific Event from SOC");

	/* Wait for command complete event */
	err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
	if ( err < 0) {
		pr_err("Failed to set patch info on Controllern");
		goto error;
	}

	if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
		pr_err("Failed to disable_event, command failure");
		return -1;
	}
	pr_info("success");

error:
	return err;

}

#if CHECK_FW
/******************************************************************************
**  check fw
******************************************************************************/
int iccm_read_off = 0;
int dccm_read_off = 0;

int iccm_j = 0;
int dccm_j = 0;

static int check_download_fwiccm(int fd)
{
	char *p_tmp;
	char *p_name;
	int reg_data,cmp_data;
	int BT_fwICCM_len;

	int err = -1;
	int size;
	char * cmd_hdr = NULL;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];

	BT_fwICCM_len = (int)sizeof(BT_fwICCM);
	while (iccm_j < BT_fwICCM_len)
	{
		memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

		iccm_read_off = iccm_j;
		cmd_hdr = (void *) (cmd + 1);
		cmd[0]	= HCI_COMMAND_PKT;
		UINT16_TO_STREAM(cmd_hdr, TCI_READ_REG);
		*cmd_hdr++ = 0x04;
		UINT32_TO_STREAM(cmd_hdr, iccm_read_off);

		size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + 0x04);

		err = write(fd, cmd, size);
		if (err != size) {
			pr_err("Send failed with ret value: %d", err);
			goto error;
		}
		/*rsp deal with ok, handle to memset*/
		memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

		/*Wait for command complete event*/
		err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
		if ( err < 0) {
			pr_err("Failed to set patch info on Controller\n");
			goto error;
		}

		if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
			pr_err("Failed to tci_write, command failure");
			return -1;
		}

		p_tmp = p_name = (char *)(rsp + 1) + \
				 HCI_EVT_CMD_CMPL_LOCAL_NAME_STRING;
		reg_data = *p_tmp + ((*(p_tmp + 1)) << 8) + ((*(p_tmp + 2)) << 16) + ((*(p_tmp + 3)) << 24)
			   +((*(p_tmp + 4)) << 32) + ((*(p_tmp + 5)) << 40) + ((*(p_tmp + 6)) << 48) +((*(p_tmp + 7)) << 56);
		cmp_data = (BT_fwICCM[iccm_read_off]) + (BT_fwICCM[iccm_read_off + 1] << 8)
			   + (BT_fwICCM[iccm_read_off + 2] << 16) + (BT_fwICCM[iccm_read_off + 3] << 24)
			   + (BT_fwICCM[iccm_read_off + 4] << 32) + (BT_fwICCM[iccm_read_off + 5] << 40)
			   + (BT_fwICCM[iccm_read_off + 6] << 48) + (BT_fwICCM[iccm_read_off + 7] << 56);


		pr_info("reg_data = %x,cmp_data = %x", reg_data, cmp_data);
		if (cmp_data == reg_data)
		{
			pr_info("read iccm OK");
		}
		else
		{
			pr_err("read iccm Fail");
			return -1;
		}
		iccm_j = iccm_j + 8;
	}
	pr_info("check iccm_fw is ok");

error:

	iccm_read_off = 0;
	iccm_j = 0;
	return err;

}

static int check_download_dccmfw(int fd)
{
	char *p_tmp;
	char *p_name;
	int reg_data = 0 ,cmp_data = 0;
	int BT_fwDCCM_len = 0;

	int err = -1;
	int size;
	char * cmd_hdr = NULL;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];

	BT_fwDCCM_len = (int)sizeof(BT_fwDCCM);
	while (dccm_j < BT_fwDCCM_len)
	{
		memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

		dccm_read_off = dccm_j;

		cmd_hdr = (void *) (cmd + 1);
		cmd[0]	= HCI_COMMAND_PKT;
		UINT16_TO_STREAM(cmd_hdr, TCI_READ_REG);
		*cmd_hdr++ = 0x04;
		UINT32_TO_STREAM(cmd_hdr, dccm_read_off);

		size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + 0x04);

		err = write(fd, cmd, size);
		if (err != size) {
			pr_err("Send failed with ret value: %d", err);
			goto error;
		}
		/*rsp deal with ok, handle to memset*/
		memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

		/*Wait for command complete event*/
		err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
		if ( err < 0) {
			pr_err("Failed to set patch info on Controller\n");
			goto error;
		}

		if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
			pr_err("Failed to tci_write, command failure");
			return -1;
		}

		p_tmp = p_name = (char *)(rsp + 1) + \
				 HCI_EVT_CMD_CMPL_LOCAL_NAME_STRING;
		reg_data = *p_tmp + ((*(p_tmp + 1)) << 8) + ((*(p_tmp + 2)) << 16) + ((*(p_tmp + 3)) << 24);
			  // +((*(p_tmp + 4)) << 32) + ((*(p_tmp + 5)) << 40) + ((*(p_tmp + 6)) << 48) +((*(p_tmp + 7)) << 56);
		cmp_data = (BT_fwDCCM[dccm_read_off]) + (BT_fwDCCM[dccm_read_off + 1] << 8)
			   + (BT_fwDCCM[dccm_read_off + 2] << 16) + (BT_fwDCCM[dccm_read_off + 3] << 24);
			  // + (BT_fwDCCM[dccm_read_off + 4] << 32) + (BT_fwDCCM[dccm_read_off + 5] << 40)
			  // + (BT_fwDCCM[dccm_read_off + 6] << 48) + (BT_fwDCCM[dccm_read_off + 7] << 56);


		pr_info("reg_data = %x,cmp_data = %x", reg_data, cmp_data);
		if (cmp_data == reg_data)
		{
			//pr_info("read dccm OK");
		}
		else
		{
			pr_err("read dccm Fail");
			return -1;
		}

		dccm_j +=  4;
	}
	pr_info("check dccm_fw is ok");

error:

	dccm_read_off = 0;
	dccm_j = 0;
	return err;

}

#endif  //END CHECK_FW

static int aml_send(int fd, char *buf, unsigned int buf_size, unsigned int offset, int diff)
{
	unsigned int i;
	int err = -1;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	int flags;
	int n = 0;
	char * cmd_hdr = NULL;

	if (buf == NULL)
	{
		pr_err("buf is NULL");
		return -1;
	}

	if (!diff)
	{
		pr_info("start download fw BT_fwICCM 0x%x  %d", buf_size, n);
	}
	else
	{
		pr_info("start download fw BT_fwDCCM 0x%x  %d", buf_size, n);
	}

	while (buf_size > 0)
	{
		unsigned int cmd_size = 0;
		unsigned int data_len = 0;
		unsigned int cmd_len = 0;
		memset(cmd, 0x0, HCI_MAX_CMD_SIZE);
		memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

		/*fw download len */
		data_len = (buf_size > RW_OPERATION_SIZE) ? RW_OPERATION_SIZE : buf_size;
		cmd_len = data_len + 4;

		cmd_hdr = (void *) (cmd + 1);
		cmd[0]	= HCI_COMMAND_PKT;
		UINT16_TO_STREAM(cmd_hdr, TCI_DOWNLOAD_BT_FW);

		/* parameter length */
		*cmd_hdr++ = cmd_len;
		if (!diff)
		{
			UINT32_TO_STREAM(cmd_hdr, ICCM_RAM_BASE + offset);
			n++;
		}

		if (diff)
		{
			UINT32_TO_STREAM(cmd_hdr, DCCM_RAM_BASE + offset);
			n++;
		}

		for (i = 0; i < data_len; i++)
		{
			cmd_hdr[i] = *(buf + offset + i);
		}

		/* Total length */
		cmd_size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd_len);

		err = write(fd, cmd, cmd_size);
		if (err != cmd_size) {
			pr_err("Send failed with ret value: %d", err);
			goto error;
		}

		/*Wait for command complete event*/
		err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
		if ( err < 0) {
			pr_err("Failed to set patch info on Controller\n");
			goto error;
		}

		if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
			pr_err("Failed to tci_write, command failure");
			return -1;
		}

		offset += data_len;
		buf_size -= data_len;
	}

	if (!diff)
	{
		pr_info("download fw BT_fwICCM SUCCESS times: %d", n);
	}
	else
	{
		pr_info("download fw BT_fwDCCM SUCCESS times: %d", n);
	}

error:
	return err;

}

/*******************************************************************************
**
** Function        aml_userial_vendor_set_baud
**
** Description     Set new baud rate
**
** Returns         None
**
*******************************************************************************/
static void aml_userial_vendor_set_baud(unsigned char userial_baud)
{
	unsigned int tcio_baud;
	pr_info("## aml_userial_vendor_set_baud: %d", userial_baud);

	if (tcgetattr(vnd_userial.fd, &vnd_userial.termios) < 0) {
		perror("Can't get port settings");
		return;
	}
	cfmakeraw(&vnd_userial.termios);
	vnd_userial.termios.c_cflag |= CLOCAL;
	vnd_userial.termios.c_cflag |= CREAD;
	vnd_userial.termios.c_cflag |= CS8;
	tcsetattr(vnd_userial.fd, TCSANOW, &vnd_userial.termios);

	aml_userial_to_tcio_baud(userial_baud, &tcio_baud);

	cfsetospeed(&vnd_userial.termios, tcio_baud);
	cfsetispeed(&vnd_userial.termios, tcio_baud);
	tcsetattr(vnd_userial.fd, TCSADRAIN, &vnd_userial.termios); /* don't change speed until last write done */

}

static int get_wifi_dev_type(char *dev_type)
{
	int fd;

	fd = open("/dev/wifi_power", O_RDWR);
	if (fd < 0) {
		return -1;
	}

	if (ioctl(fd, SDIO_GET_DEV_TYPE, dev_type) < 0) {
		close(fd);

		return -1;
	}
	close(fd);

	return 0;
}

static int chip_to_module(char* chipid)
{
	int i;
	for (i = 0; i < (int)(ARRAY_SIZE(aml_chip)); i++)
	{
		if (!strncmp(chipid, aml_chip[i].chipid, 6))
		{
			return aml_chip[i].module_type;
		}
	}
	return 0;
}

static int select_module(void)
{
	char dev_type[10] = {'\0'};
	char file_name[100] = {'\0'};
	char pid_buf[128];

	FILE *fp = NULL;

	memset(pid_buf, 0, sizeof(pid_buf));
	// sdio
	get_wifi_dev_type(dev_type);
	sprintf(file_name, "/sys/bus/mmc/devices/%s:0000/%s:0000:1/device", dev_type, dev_type);

	fp = fopen(file_name, "r");

	if (!fp) {
		fprintf(stderr, "open sdio wifi file failed\n");
	} else {
		if (fread(pid_buf, 1, sizeof(pid_buf)-1, fp) > 0) {
			goto out;
		}
		fclose(fp);
		fprintf(stderr, "read sdio wifi pid failed\n");
	}

	// mainline mmc core names SDIO functions mmcN:RCA:FN, not sdiob:0000:1
	for (int n = 0; n < 4; n++) {
		sprintf(file_name, "/sys/bus/mmc/devices/mmc%d:0000/mmc%d:0000:1/device", n, n);
		fp = fopen(file_name, "r");
		if (!fp)
			continue;
		if (fread(pid_buf, 1, sizeof(pid_buf)-1, fp) > 0)
			goto out;
		fclose(fp);
		fp = NULL;
	}

	// pcie
	memset(file_name, 0, sizeof(file_name));
	sprintf(file_name, "/sys/bus/pci/devices/0000:01:00.0/device");

	fp = fopen(file_name, "r");
	if (!fp) {
		fprintf(stderr, "open pcie wifi file failed\n");
	} else {
		if (fread(pid_buf, 1, sizeof(pid_buf)-1, fp) > 0) {
			goto out;
		}
		fclose(fp);
		fprintf(stderr, "read pcie wifi pid failed\n");
	}

	// usb
	/*
	memset(file_name, 0, sizeof(file_name));
	sprintf(file_name, "/sys/bus/usb/devices/%d-%d/idProduct",i,j);
	// file_name ~ /sys/bus/usb/devices/1-1/idProduct
	if (!fp) {
		fprintf(stderr, "open usb wifi file failed\n");
	} else {
		if (fread(pid_buf, 1, sizeof(pid_buf)-1, fp) > 0) {
			goto out;
		}
		fclose(fp);
		fprintf(stderr, "read usb wifi pid failed\n");
	}*/

out:
	pid_buf[sizeof(pid_buf)-1] = '\0';
	if (fp)
		fclose(fp);
	pr_info("chip id is %s",pid_buf);

	return chip_to_module(pid_buf);
}

static int get_aml_wifi_module(void)
{
	int i;
	char aml_wifi_module[20];
	int fd = open("/dev/wifi_power", O_RDWR);
	if (fd < 0) {
		pr_info("open /dev/wifi_power fail\n");
		return -1;
	}

	if (ioctl (fd, GET_AML_WIFI_MODULE, aml_wifi_module) < 0) {
		pr_info("ioctl GET_AML_WIFI_MODULE fail\n");
		close(fd);
		return -1;
	}
	close(fd);

	for (i = 0; i < sizeof(aml_module)/sizeof(vnd_module_t); i++) {
		if (!strcmp(aml_module[i].name, aml_wifi_module)) {
			pr_info("match aml_module: %s\n", aml_module[i].name);
			return i;
		}
	}

	return -1;
}

static int select_firmware(char ** file)
{
	int i, idx, module_type;

	idx = get_aml_wifi_module();

	if (idx < 0) {
		module_type = select_module();

		for (i = 0; i < sizeof(aml_module)/sizeof(vnd_module_t); i++)
		{
			if (aml_module[i].module_type == module_type) {
				idx = i;
				break;
			}
		}
	}

	if (idx >= 0) {
		*file = aml_module[idx].fw_file;
		AML_MODULE = aml_module[idx].module_type;
	}

	return idx;
}

static unsigned int hw_config_get_iccm_size(char * file)
{
	int fd = 0;
	unsigned int  iccm_size = 0;
	int size = 0;
	if ((fd = open(file, O_RDONLY)) < 0)
		return 0;
	size = read(fd, &iccm_size, 4);
	if (size < 0)
	{
	    pr_err("--------- read error!---------");
	    close(fd);
	    return 0;
	}
	close(fd);

	pr_info("--------- iccm_size %d---------\n", iccm_size);
	return (iccm_size < ICCM_DCCM_MAX) ? iccm_size : ICCM_DCCM_MAX;
}

static unsigned int hw_config_get_dccm_size(char * file)
{
	int fd = 0;
	unsigned int dccm_size = 0;
	int size = 0;
	if ((fd = open(file, O_RDONLY)) < 0)
		return 0;

	if (lseek(fd, 4, SEEK_SET) != 4)
	{
		pr_err("skip 4 bytes iccm len fail");
		close(fd);
		return 0;
	}

	size = read(fd, &dccm_size, 4);
	if (size < 0)
	{
	    pr_err("--------- read error!---------");
	    close(fd);
	    return 0;
	}
	close(fd);

	pr_info("--------- dccm_size %d---------\n", dccm_size);
	return (dccm_size < ICCM_DCCM_MAX) ? dccm_size : ICCM_DCCM_MAX;
}

static int get_iccmbuf_dccmbuf(char **iccmbuf, char** dccmbuf, unsigned int iccmlen, unsigned int dccmlen, char * file)
{
	int fd;
	int ret =0;
	char *p_iccmbuf = malloc(iccmlen + 1);
	char *p_dccmbuf;

	if (p_iccmbuf == NULL)
	{
		pr_err("malloc p_iccmbuf fail");
		ret = 1;
		goto error;
	}
	memset(p_iccmbuf, 0, iccmlen + 1);

	p_dccmbuf = malloc(dccmlen + 1);
	if (p_dccmbuf == NULL)
	{
		pr_err("malloc p_dccmbuf fail");
		ret = 2;
		goto error;
	}
	memset(p_dccmbuf, 0, dccmlen + 1);

	fd = open(file, O_RDONLY);
	if (fd <0)
	{
		pr_err("open fw_file fail");
		ret = 3;
		goto error;
	}
	if (lseek(fd, 8, SEEK_SET) != 8)
	{
		pr_err("skip 8byte len fail");
		close(fd);
		ret = 3;
		goto error;
	}
	ret = read(fd, p_iccmbuf, iccmlen);
	if (ret < 0)
	{
		pr_err("------ p_iccmbuf read error!------");
		close(fd);
		ret = 3;
		goto error;
	}
	ret = read(fd, p_dccmbuf, dccmlen);
	if (ret < 0)
	{
		pr_err("------ p_dccmbuf read error!------");
		close(fd);
		ret = 3;
		goto error;
	}

	close(fd);
	*iccmbuf = p_iccmbuf;
	*dccmbuf = p_dccmbuf;
	return 0;

error:
	if (ret == 1)
	{
		//do nothing
	}
	else if (ret == 2)
	{
		free(p_iccmbuf);
	}
	else if (ret ==3)
	{
		free(p_iccmbuf);
		free(p_dccmbuf);
	}

	return 1;
}

static int aml_download_fw_file(int fd, callback func)
{
	int err = -1;
	int idx = 0;
	unsigned int fwICCM_len =0;
	unsigned int fwICCM_size = 0 ;
	unsigned int fwICCM_offset =0 ;
	char * p_BT_fwICCM = NULL;

	unsigned int fwDCCM_size = 0;
	unsigned int fwDCCM_offset = 0;
	char * p_BT_fwDCCM = NULL;

	char *fw_file = NULL;

	idx = select_firmware(&fw_file);
	if (idx < 0)
	{
		pr_err("no matched fw\n");
		return err;
	}

	pr_info("%s start download: %s",aml_module[idx].mod_type_name, fw_file);

	fwICCM_len = hw_config_get_iccm_size(fw_file);
	fwICCM_size = fwICCM_len;
	fwICCM_size -= aml_module[idx].iccm_base * 1024;
	pr_info("fw BT_fwICCM is total : 0x%x", fwICCM_size);
	fwICCM_offset = aml_module[idx].iccm_base * 1024;

	fwDCCM_size = hw_config_get_dccm_size(fw_file);
	pr_info("fw BT_fwDCCM is total : 0x%x", fwDCCM_size);

	if (get_iccmbuf_dccmbuf(&p_BT_fwICCM, &p_BT_fwDCCM, fwICCM_len, fwDCCM_size, fw_file))
	{
		pr_err("get_iccmbuf_dccmbuf fail");
		return err;
	}

	err = aml_send(fd, p_BT_fwICCM, fwICCM_size, fwICCM_offset, fwICCM);
	if (err < 0)
	{
		pr_err("write BT_fwICCM fail");
		goto error;
	}
#if CHECK_FW
	pr_info("start check BT_fwICCM");
	err = check_download_fwiccm(fd);
	if (err < 0)
	{
		pr_err("check_download_fwiccm fail");
	}
#endif

	err = aml_send(fd, p_BT_fwDCCM, fwDCCM_size, fwDCCM_offset, fwDCCM);
	if (err < 0)
	{
		pr_err("write BT_fwDCCM fail");
		goto error;
	}
#if CHECK_FW
	pr_info("start check BT_fwDCCM");
	err = check_download_dccmfw(fd);
	if (err < 0)
	{
		pr_err("check_download_dccmfw fail");
	}
#endif

	if (func != NULL)
	{
		err = func(fd);
		if (err < 0)
		{
			pr_err("cmd fail");
			err = -1;
		}
	}
	else
	{
		pr_err("func is NULL");
		err = -1;
	}

error:
	free(p_BT_fwICCM);
	free(p_BT_fwDCCM);
	return err;

}

static void aml_flow_control(int fd, int opt)
{
	struct termios c_opt;

	ioctl(fd, TIOCMGET, &c_opt);
	c_opt.c_cc[VTIME] = 0; /* inter-character timer unused */
	c_opt.c_cc[VMIN] = 0; /* blocking read until 8 chars received */
	c_opt.c_cflag &= ~CSIZE;
	c_opt.c_cflag |= (CS8 | CLOCAL | CREAD);
	if (MSM_ENABLE_FLOW_CTRL)
		c_opt.c_cflag |= CRTSCTS;
	else if (MSM_DISABLE_FLOW_CTRL)
		c_opt.c_cflag |= ~CRTSCTS;
	else {
		pr_err("Incorrect option passed for TIOCMSET");
		return;
	}
	c_opt.c_iflag = IGNPAR;
	c_opt.c_oflag = 0;
	c_opt.c_lflag = 0;
	ioctl(fd, TIOCMSET, &c_opt);
}


static int aml_update_baudrate(int fd, callback tci_write, callback tci_read)
{
	int size, err = 0;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	hci_command_hdr *cmd_hdr;
	int flags;

	memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

	cmd_hdr = (void *) (cmd + 1);
	cmd[0]	= HCI_COMMAND_PKT;
	cmd_hdr->opcode = TCI_UPDATE_UART_BAUDRATE;
	cmd_hdr->plen	= 0x08;
	cmd[4] = 0x28;
	cmd[5] = 0x01;
	cmd[6] = 0xa3;
	cmd[7] = 0x00;
	cmd[8] = 0x09;
	cmd[9] = 0x70;
	cmd[10]= 0x00;
	cmd[11]= 0x00;

	/* Total length */
	size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd_hdr->plen);

	/* Send the HCI command packet to UART for transmission */
	pr_info("HCI CMD: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",cmd[0], cmd[1], cmd[2], cmd[3],\
					 cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9], cmd[10], cmd[11]) ;
	err = write(fd, cmd, size);
	if (err != size) {
		pr_err("Send failed with ret value: %d", err);
		goto error;
	}
	pr_info("Received HCI-Vendor Specific Event from SOC");

	/* Wait for command complete event */
	err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
	if ( err < 0) {
		pr_err("Failed to set patch info on Controllern");
		goto error;
	}
	/* Flow off during baudrate change */
	aml_flow_control(fd, MSM_DISABLE_FLOW_CTRL);

	/* Change Local UART baudrate to high speed UART */
	aml_userial_vendor_set_baud(USERIAL_BAUD_4M);

	/* Flow on after changing local uart baudrate */
	aml_flow_control(fd, MSM_ENABLE_FLOW_CTRL);

	pr_info("success");

	if (tci_write != NULL)
	{
		err = tci_write(fd);
		if (err < 0)
		{
			pr_err("tci_write cmd fail");
			goto error;
		}
	}
	else
	{
		pr_err("tci_write is NULL");
		return -1;
	}

	if (tci_read != NULL)
	{
		err = tci_read(fd);
		if (err < 0)
		{
			pr_err("tci_read cmd fail");
			goto error;
		}
	}
	else
	{
		pr_err("tci_read is NULL");
		return -1;
	}

error:
	return err;

}

static int aml_tci_write_reg(int fd)
{
	int size, err = 0;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	hci_command_hdr *cmd_hdr;
	int flags;

	memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

	cmd_hdr = (void *) (cmd + 1);
	cmd[0]	= HCI_COMMAND_PKT;
	cmd_hdr->opcode = TCI_WRITE_REG;
	cmd_hdr->plen	= 0x08;
	cmd[4] = 0x14;
	cmd[5] = 0x00;
	cmd[6] = 0xa7;
	cmd[7] = 0x00;
	cmd[8] = 0x00;
	cmd[9] = 0x00;
	cmd[10]= 0x00;
	cmd[11]= 0x01;

	/* Total length of the packet to be sent to the Controller */
	size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd_hdr->plen);

	/* Send the HCI command packet to UART for transmission */
	pr_info("HCI CMD: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",cmd[0], cmd[1], cmd[2], cmd[3],\
					 cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9], cmd[10], cmd[11]) ;
	err = write(fd, cmd, size);
	if (err != size) {
		pr_err("Send failed with ret value: %d", err);
		goto error;
	}

	/* Wait for command complete event */
	err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
	if ( err < 0) {
		pr_err("Failed to set patch info on Controller\n");
		goto error;
	}
	if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
		pr_err("Failed to tci_write, command failure");
		return -1;
	}

	pr_info("continue tci_write");
	memset(cmd, 0x0, HCI_MAX_CMD_SIZE);
	memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

	cmd_hdr = (void *) (cmd + 1);
	cmd[0]	= HCI_COMMAND_PKT;
	cmd_hdr->opcode = TCI_WRITE_REG;
	cmd_hdr->plen	= 0x08;
	cmd[4] = 0x50;
	cmd[5] = 0x30;
	cmd[6] = 0xf0;
	cmd[7] = 0x00;
	cmd[8] = 0x00;
	cmd[9] = 0x00;
	cmd[10]= 0x00;
	cmd[11]= 0x00;
	/*Total length of the packet*/
	size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd_hdr->plen);
	pr_info("HCI CMD: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",cmd[0], cmd[1], cmd[2], cmd[3],\
					 cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9], cmd[10], cmd[11]) ;
	err = write(fd, cmd, size);
	if (err != size) {
		pr_err("Send failed with ret value: %d", err);
		goto error;
	}
	/* Wait for command complete event */
	err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
	if ( err < 0) {
		pr_info("Failed to set patch info on Controller");
		goto error;
	}
	if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
		pr_err("Failed to tci_write, command failure");
		return -1;
	}
	pr_info("success");

error:
	return err;

}

static int aml_tci_read_reg(int fd)
{
	int size, err = -1;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	hci_command_hdr *cmd_hdr;
	int flags;

	memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

	cmd_hdr = (void *) (cmd + 1);
	cmd[0]	= HCI_COMMAND_PKT;
	cmd_hdr->opcode = TCI_READ_REG;
	cmd_hdr->plen	= 0x04;
	cmd[4] = 0x50;
	cmd[5] = 0x30;
	cmd[6] = 0xf0;
	cmd[7] = 0x00;

	/* Total length of the packet to be sent to the Controller */
	size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd_hdr->plen);

	/* Send the HCI command packet to UART for transmission */
	pr_info("HCI CMD: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",\
				 cmd[0], cmd[1], cmd[2], cmd[3],cmd[4], cmd[5], cmd[6], cmd[7]) ;
	err = write(fd, cmd, size);
	if (err != size) {
		pr_err("Send failed with ret value: %d", err);
		goto error;
	}
	/* Wait for command complete event */
	err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
	if ( err < 0) {
		pr_err("Failed to set patch info on Controller");
		goto error;
	}
	if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
		pr_err("Failed to tci_write, command failure");
		return -1;
	}
	pr_info("success");

error:
	return err;

}

static int aml_hci_reset(int fd)
{
	int size, err = 0;
	unsigned char cmd[HCI_MAX_CMD_SIZE];
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	hci_command_hdr *cmd_hdr;
	int flags;

	pr_info("HCI RESET");

	memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

	cmd_hdr = (void *) (cmd + 1);
	cmd[0]  = HCI_COMMAND_PKT;
	cmd_hdr->opcode = HCI_RESET;
	cmd_hdr->plen   = 0;

	/* Total length of the packet to be sent to the Controller */
	size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE);

	/* Send the HCI command packet to UART for transmission */
	pr_info("HCI CMD: 0x%x 0x%x 0x%x 0x%x", cmd[0], cmd[1], cmd[2], cmd[3]);
	err = write(fd, cmd, size);
	if (err != size) {
		pr_err("Send failed with ret value: %d", err);
		goto error;
	}
	pr_info("Received HCI-Vendor Specific Event from SOC");

	/* Wait for command complete event */
	err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
	if ( err < 0) {
		pr_err("Failed to set patch info on Controller");
		goto error;
	}

	if (rsp[4] != cmd[1] || rsp[5] != cmd[2] || rsp[6] != 0X00) {
		pr_err("Failed to tci_write, command failure");
		return -1;
	}
	pr_info("success");

error:
	return err;

}

int aml_init(int fd, char *bdaddr)
{
	int err = -1;
	int size;

	vnd_userial.fd = fd;

	if (!get_config(AML_BT_CONFIG_RF_FILE)) {
		pr_err("get_config fail,use default value");
	}

	/* update baud */
	err = aml_update_baudrate(fd, aml_tci_write_reg, aml_tci_read_reg);
	if (err < 0) {
		pr_err("Baud rate change failed!");
		goto error;
	}
	pr_info("Baud rate changed successfully");

	/* Download fw files */
	err = aml_download_fw_file(fd, aml_disable_event);
	if (err < 0) {
		pr_err("Download fw file failed!");
		goto error;
	}
	pr_info("Download fw file successfully");
	err = hw_config_set_rf_params(fd);
	if (err < 0) {
		pr_err("hw_config_set_rf_params failed!");
		goto error;
	}
	/*start cup cmd*/
	err = aml_start_cpu_before_cmd(fd);
	if (err < 0) {
		pr_err("cpu before cmd failed!");
		goto error;
	}
	pr_info("cpu before cmd successfully");

	/* Perform HCI reset here*/
	err = aml_hci_reset(fd);
	if ( err < 0 ) {
		pr_err("HCI Reset Failed !!!");
		goto error;
	}

	pr_info("HCI Reset is done");

error:
	return err;
}
