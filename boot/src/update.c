/* @ . @ *-c-*
 *
 * Copyright (c) 2012, Beijing Hanbang Technology, Inc.
 * Copyright (c) 2012, John Lee <furious_tauren@163.com>
 *
 * All rights reserved. No Part of this file may be reproduced,
 * stored in a retrieval system, or transmitted, in any form,
 * or by any means, electronic, mechanical, photocopying, recording,
 * or otherwise, without the prior consent of HanBang, Inc.
 */

#include <common.h>
#include <command.h>
#include <net.h>
#include <malloc.h>
#include <image.h>
#include <asm/byteorder.h>
#include <spi_flash.h>
#include <linux/mtd/mtd.h>
#include <linux/ctype.h>
#include <u-boot/md5.h>

#define FW_MAGIC 0xa5a5a5a5
#define PART_NUM 4

#define FNLIST "dir.txt"
#define LOADADDR (void *)0x82000000

/* IPCB_Vx.x.xx.xxxx_UPDATE.update */
#define FN_PREFIX "IPCB_V"
#define FN_SUFFIX "_UPDATE.update"

extern ulong TftpRRQTimeoutMSecs;
extern int TftpRRQTimeoutCountMax;
extern ulong load_addr;
extern int do_reset (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);

struct part_info {

	int magic;
	int index;

	ulong start;
	size_t size;

	char md5[16];
};

struct part_head {
	char fw_ver[32];
	char app_ver[32]; /* used by web update */
	struct part_info fwparts_info[PART_NUM];
};

static struct part_head ph;

static int update_flash(const void *data, ulong offset, size_t sz, ulong entry)
{
	int old_ctrlc = disable_ctrlc(0);
	struct spi_flash *flash = spi_flash_probe(0, 0, 1000000, 0x3);
	if (!flash) {
		printf("Failed to initialize SPI flash\n");
		return 1;
	}

	printf("Flash Erasing ...\n");
	if (flash->erase(flash, offset, entry)) {
		printf("Failed: SPI flash erase failed\n");
		return 1;
	}

	printf("Flash Writing ...\n");
	if (flash->write(flash, offset, sz, data)) {
		printf("Failed: SPI flash write failed\n");
		return 1;
	}

	printf("Succeed: offset=0x%08lx, size=0x%08lx\n", offset, (ulong)sz);

	/* restore the old state */
	disable_ctrlc(old_ctrlc);
	return 0;
}

static int update_load(char *filename, ulong msec_max, void *addr)
{
	int size;
	ulong saved_timeout_msecs;
	int saved_timeout_count;
	char *saved_netretry, *saved_bootfile, *saved_phy_link_time;

	/* save used globals and env variable */
	saved_timeout_msecs = TftpRRQTimeoutMSecs;
	saved_timeout_count = TftpRRQTimeoutCountMax;
	saved_phy_link_time = strdup(getenv("phy_link_time"));
	saved_netretry = strdup(getenv("netretry"));
	saved_bootfile = strdup(BootFile);

	/* set timeouts for auto-update */
	TftpRRQTimeoutMSecs = msec_max;
	TftpRRQTimeoutCountMax = 0; /* no retry */

	/*XXX: to reduce net link wait time */
	setenv("phy_link_time", "50");

	/* we don't want to retry the connection if errors occur */
	setenv("netretry", "no");

	/* download the update file */
	load_addr = (ulong)addr;
	copy_filename(BootFile, filename, sizeof(BootFile));
	size = NetLoop(TFTP);

	if (size > 0)
		flush_cache(load_addr, size);

	/* restore changed globals and env variable */
	TftpRRQTimeoutMSecs = saved_timeout_msecs;
	TftpRRQTimeoutCountMax = saved_timeout_count;

	setenv("phy_link_time", saved_phy_link_time);
	if (saved_phy_link_time != NULL)
		free(saved_phy_link_time);

	setenv("netretry", saved_netretry);
	if (saved_netretry != NULL)
		free(saved_netretry);

	if (saved_bootfile != NULL) {
		copy_filename(BootFile, saved_bootfile, sizeof(BootFile));
		free(saved_bootfile);
	}

	return size;
}

/* Format of date: yymmdd */
static int is_valid_date(int date)
{
	int y, m, d;
	int month[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

	y = date / 10000 + 2000;
	m = (date % 10000) / 100;
	d = date % 100;

	if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)
		month[1] = 29;

	return y <= 2099 && m > 0 && m <= 12 && d > 0 && d <= month[m - 1];
}

static int get_vernum(const char *s, u32 l)
{
	int cn[4] = {1, 1, 2, 4};
	int ver = 0;
	int i;

	if (l != strlen("IPCB_Vx.x.xx.xxxx_UPDATE.update"))
		return -1;

	l = strlen(FN_PREFIX);
	if (strncmp(s, FN_PREFIX, l))
		return -1;

	s = s + l;
	for (l = i = 0; *s != '_'; s++) { /* check x.x.xx.xxxx */
		if (*s == '.' && l == cn[i]) {
			l = 0;
			i++;
		} else if (isdigit(*s)) {
			l++;
			ver = ver * 10 + *s - '0';
		} else {
			return -1;
		}
	}

	if (l != cn[3])
		return -1;

	if (strncmp(s, FN_SUFFIX, strlen(FN_SUFFIX)))
		return -1;

	if (ver / 1000000 > 99 || ver / 1000000 <= 0)
		return -1;

	if (!is_valid_date(ver % 1000000))
		return -1;

	return ver;
}

static char *get_firmware_filename(char *addr, char *filename)
{
        int tmp, max = 0;
	char *s, *s1, *p = NULL;
	int size;

	size = update_load(FNLIST, 100, addr);
	if (size <= 0)
		return NULL;

	for (s = s1 = addr; s - addr <= size; s = s1) {

		while (s1 - addr <= size && !isspace(*s1))
			s1++;

		tmp = get_vernum(s, s1 - s);
		if (max < tmp) {
			max = tmp;
			p = s;
		}

		*s1++ = '\0';
		while (s1 - addr <= size && isspace(*s1))
			s1++;
	}

	if (p)
		copy_filename(filename, p, 32);

	return p;
}

static int is_need_update(void *data, char *filename)
{
	int i, j;
	unsigned char md5[16];
	struct spi_flash *flash;

	flash = spi_flash_probe(0, 0, 1000000, 0x3);
	if (!flash) {
		puts("Failed to initialize SPI flash\n");
		return 0;
	}

	if (flash->read(flash, CONFIG_IPNC_ENV_OFFSET, sizeof(ph), &ph)) {
		puts("Fails to read ph from SPI flash\n");
		return 0;
	}

	if (get_vernum(ph.fw_ver, 31) <= 0) {
		puts("\nInvalid version infomation, update is scheduled\n\n");
		return 1;
	}
	printf("\nCurrent firmware version: %s\n", ph.fw_ver);

	if (strncmp(ph.fw_ver, filename, 32) < 0) {
		puts("Higher version found, update is scheduled\n\n");
		return 1;
	}

	for (i = 0; i < PART_NUM - 1; i++) { /**/

		if (ph.fwparts_info[i].magic != FW_MAGIC) {
			printf("Invalid partition%d magic,"
					" update is scheduled\n\n", i);
			return 1;
		}

		if (flash->read(flash, ph.fwparts_info[i].start,
				       	ph.fwparts_info[i].size, data)) {
			puts("Fails to read data from SPI flash\n");
			return 0;
		}

		md5_wd((u8 *)data, ph.fwparts_info[i].size, md5, CHUNKSZ_MD5);
		if (memcmp (md5, ph.fwparts_info[i].md5, 16) != 0) {
			puts("System corrupted, update is scheduled\n");
			printf("md5(partition%02d): ", i);
			for (j = 0; j < 16; j++)
				printf("%02x", ph.fwparts_info[i].md5[j]);
			puts("\n\n");
			return 1;
		}
	}

#if 0
	/* If appfs has been setup, check its hash value */
	if (ph.fwparts_info[i].magic == FW_MAGIC) {
		if (flash->read(flash, ph.fwparts_info[i].start,
					ph.fwparts_info[i].size, data)) {
			puts("Fails to read data from SPI flash\n");
			return 0;
		}

		md5_wd((u8 *)data, ph.fwparts_info[i].size, md5, CHUNKSZ_MD5);
		if (memcmp (md5, ph.fwparts_info[i].md5, 16) != 0) {
			puts("Appfs corrupted, update is scheduled\n\n");
			printf("md5(partition%02d): ", i);
			for (j = 0; j < 16; j++)
				printf("%02x", ph.fwparts_info[i].md5[j]);
			puts("\n\n");
			return 1;
		}
	}
#endif

	return 0;
}

static int update_fit_get_hash(const void *fit, int noffset, u8 **value)
{
	int ndepth = 0;
	int value_len;
	u8 *val;

	/* Process all hash subnodes of the component image node */
	noffset = fdt_next_node(fit, noffset, &ndepth);
	while (noffset >= 0 && ndepth > 0) {
		if (ndepth != 1)
			goto __next_node;

		if (strncmp(fit_get_name(fit, noffset, NULL),
					FIT_HASH_NODENAME,
					strlen(FIT_HASH_NODENAME)) != 0)
			goto __next_node;

		if (fit_image_hash_get_value(fit, noffset, &val, &value_len)) {
			goto __next_node;
		} else { /* XXX: Only find out the first hash */
			*value = val;
			return 0;
		}

__next_node:
		noffset = fdt_next_node(fit, noffset, &ndepth);
	}

	return 1;
}

static void update_save_part_head(const void *fit,
	       	int noffset, ulong start, size_t size)
{
	char *part_name[PART_NUM] = {
		"u-boot", "kernel", "rootfs", "appfs"
	};
	char **desc = NULL;
	uint8_t *md5;
	int i = 0;

	if (fit_get_desc(fit, noffset, desc)) {
		puts("Failed to get fit desc, error when update.\n");
		return;
	}

	while (strcmp(part_name[i], *desc)) {
		if (i >= PART_NUM)
			return;
		i++;
	}

	if (update_fit_get_hash(fit, noffset, &md5)) {
		puts("Failed to get part hash, error when update.\n");
		return;
	}

	ph.fwparts_info[i].magic = FW_MAGIC;
	ph.fwparts_info[i].index = i;
	ph.fwparts_info[i].start = start;
	ph.fwparts_info[i].size = size;
	memcpy(ph.fwparts_info[i].md5, (const char *)md5, 16);
}

void update_tftp(void)
{
	int noffset, ndepth = 0;
	const void *data;
	ulong fladdr, entry;
       	size_t size;
	void *fit = LOADADDR;
	char filename[32];

	if (!get_firmware_filename((char *)fit, filename))
		return;

	if (!is_need_update(fit, filename))
		return;

	if (update_load(filename, 100, fit) <= 0) {
		printf("Can't get load firmware, aborting update\n");
		return;
	}

	puts("\nSystem is ready to start update ...\n" );
	puts("@::::::::::::::::::::::++++::::::::::::::::::::::@\n");

	if (!fit_check_format(fit)) {
		printf("Bad FIT format, aborting auto-update\n");
		return;
	}

	noffset = fdt_path_offset(fit, FIT_IMAGES_PATH);
	noffset = fdt_next_node(fit, noffset, &ndepth);
	while (noffset >= 0 && ndepth > 0) {

		if (ndepth != 1)
			goto next_node;

		printf("\nUpdating '%s': ", fit_get_name(fit, noffset, NULL));

		if (!fit_image_check_hashes(fit, noffset))
			goto next_node;
		printf("\n");

		if (fit_image_get_data(fit, noffset, &data, &size)) {
			printf("Can't get data section, goto next node\n");
			goto next_node;
		}

		if (fit_image_get_load(fit, noffset, &fladdr)) {
			printf("Can't get load info, goto next node\n");
			goto next_node;
		}

		if (fit_image_get_entry(fit, noffset, &entry)) {
			printf("Can't get entry info, goto next node\n");
			goto next_node;
		}

		if (update_flash(data, fladdr, size, entry))
			goto next_node;

		update_save_part_head(fit, noffset, fladdr, size);

next_node:
		noffset = fdt_next_node(fit, noffset, &ndepth);
	}

	printf ("\nSaving Environment to 0x%x...\n", CONFIG_IPNC_ENV_OFFSET);
	strcpy(ph.fw_ver, filename);
	update_flash(&ph, CONFIG_IPNC_ENV_OFFSET, sizeof(ph), 0x40000);

	puts("\n@::::::::::::::::::::::++++::::::::::::::::::::::@\n");
	printf("Succeeding in updating!\n\n");
	do_reset(NULL, 0, 0, NULL);
}

