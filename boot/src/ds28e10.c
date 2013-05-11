/* -- C -- ~ @ ~
 *
 * Copyright (c) 2012, Beijing Hanbang Technology, Inc.
 * John Lee <furious_tauren@163.com>
 *
 * All rights reserved. No Part of this file may be reproduced,
 * stored in a retrieval system, or transmitted, in any form,
 * or by any means, electronic, mechanical, photocopying, recording,
 * or otherwise, without the prior consent of HanBang, Inc.
 */

#include <common.h>
#include <config.h>
#include <asm/io.h>

#define W1_PIN			6
#define DS28E10_RETRY_CN	3

struct authentication_data {
	u8 challenge[12];
	u8 pagedata[32];
	u8 mac[22];
};

static u8 w1_crc8_table[] = {
	0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32,
	163, 253, 31, 65, 157, 195, 33, 127, 252, 162, 64, 30,
	95, 1, 227, 189, 62, 96, 130, 220, 35, 125, 159, 193,
	66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
	190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158,
	29, 67, 161, 255, 70, 24, 250, 164, 39, 121, 155, 197,
	132, 218, 56, 102, 229, 187, 89, 7, 219, 133, 103, 57,
	186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
	101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69,
	198, 152, 122, 36, 248, 166, 68, 26, 153, 199, 37, 123,
	58, 100, 134, 216, 91, 5, 231, 185, 140, 210, 48, 110,
	237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
	17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49,
	178, 236, 14, 80, 175, 241, 19, 77, 206, 144, 114, 44,
	109, 51, 209, 143, 12, 82, 176, 238, 50, 108, 142, 208,
	83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
	202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234,
	105, 55, 213, 139, 87, 9, 235, 181, 54, 104, 138, 212,
	149, 203, 41, 119, 244, 170, 72, 22, 233, 183, 85, 11,
	136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
	116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84,
	215, 137, 107, 53
};

static u8 crc8(u8 *data, int len)
{
	u8 crc = 0;

	while (len--)
		crc = w1_crc8_table[crc ^ *data++];

	return crc;
}

static u16 const crc16_table[256] = {
	0x0000, 0xC0C1, 0xC181, 0x0140, 0xC301, 0x03C0, 0x0280, 0xC241,
	0xC601, 0x06C0, 0x0780, 0xC741, 0x0500, 0xC5C1, 0xC481, 0x0440,
	0xCC01, 0x0CC0, 0x0D80, 0xCD41, 0x0F00, 0xCFC1, 0xCE81, 0x0E40,
	0x0A00, 0xCAC1, 0xCB81, 0x0B40, 0xC901, 0x09C0, 0x0880, 0xC841,
	0xD801, 0x18C0, 0x1980, 0xD941, 0x1B00, 0xDBC1, 0xDA81, 0x1A40,
	0x1E00, 0xDEC1, 0xDF81, 0x1F40, 0xDD01, 0x1DC0, 0x1C80, 0xDC41,
	0x1400, 0xD4C1, 0xD581, 0x1540, 0xD701, 0x17C0, 0x1680, 0xD641,
	0xD201, 0x12C0, 0x1380, 0xD341, 0x1100, 0xD1C1, 0xD081, 0x1040,
	0xF001, 0x30C0, 0x3180, 0xF141, 0x3300, 0xF3C1, 0xF281, 0x3240,
	0x3600, 0xF6C1, 0xF781, 0x3740, 0xF501, 0x35C0, 0x3480, 0xF441,
	0x3C00, 0xFCC1, 0xFD81, 0x3D40, 0xFF01, 0x3FC0, 0x3E80, 0xFE41,
	0xFA01, 0x3AC0, 0x3B80, 0xFB41, 0x3900, 0xF9C1, 0xF881, 0x3840,
	0x2800, 0xE8C1, 0xE981, 0x2940, 0xEB01, 0x2BC0, 0x2A80, 0xEA41,
	0xEE01, 0x2EC0, 0x2F80, 0xEF41, 0x2D00, 0xEDC1, 0xEC81, 0x2C40,
	0xE401, 0x24C0, 0x2580, 0xE541, 0x2700, 0xE7C1, 0xE681, 0x2640,
	0x2200, 0xE2C1, 0xE381, 0x2340, 0xE101, 0x21C0, 0x2080, 0xE041,
	0xA001, 0x60C0, 0x6180, 0xA141, 0x6300, 0xA3C1, 0xA281, 0x6240,
	0x6600, 0xA6C1, 0xA781, 0x6740, 0xA501, 0x65C0, 0x6480, 0xA441,
	0x6C00, 0xACC1, 0xAD81, 0x6D40, 0xAF01, 0x6FC0, 0x6E80, 0xAE41,
	0xAA01, 0x6AC0, 0x6B80, 0xAB41, 0x6900, 0xA9C1, 0xA881, 0x6840,
	0x7800, 0xB8C1, 0xB981, 0x7940, 0xBB01, 0x7BC0, 0x7A80, 0xBA41,
	0xBE01, 0x7EC0, 0x7F80, 0xBF41, 0x7D00, 0xBDC1, 0xBC81, 0x7C40,
	0xB401, 0x74C0, 0x7580, 0xB541, 0x7700, 0xB7C1, 0xB681, 0x7640,
	0x7200, 0xB2C1, 0xB381, 0x7340, 0xB101, 0x71C0, 0x7080, 0xB041,
	0x5000, 0x90C1, 0x9181, 0x5140, 0x9301, 0x53C0, 0x5280, 0x9241,
	0x9601, 0x56C0, 0x5780, 0x9741, 0x5500, 0x95C1, 0x9481, 0x5440,
	0x9C01, 0x5CC0, 0x5D80, 0x9D41, 0x5F00, 0x9FC1, 0x9E81, 0x5E40,
	0x5A00, 0x9AC1, 0x9B81, 0x5B40, 0x9901, 0x59C0, 0x5880, 0x9841,
	0x8801, 0x48C0, 0x4980, 0x8941, 0x4B00, 0x8BC1, 0x8A81, 0x4A40,
	0x4E00, 0x8EC1, 0x8F81, 0x4F40, 0x8D01, 0x4DC0, 0x4C80, 0x8C41,
	0x4400, 0x84C1, 0x8581, 0x4540, 0x8701, 0x47C0, 0x4680, 0x8641,
	0x8201, 0x42C0, 0x4380, 0x8341, 0x4100, 0x81C1, 0x8081, 0x4040
};

static inline u16 crc16_byte(u16 crc, const u8 data)
{
	return (crc >> 8) ^ crc16_table[(crc ^ data) & 0xff];
}

static u16 crc16(u16 crc, u8 const *buffer, u32 len)
{
	while (len--)
		crc = crc16_byte(crc, *buffer++);
	return crc;
}

static inline void ds28e10_master_pulldown(u32 pin)
{
	u32 val;
	u32 reg_base = GPIO0_REG_BASE + pin / 8 * 0x10000;

	val = __raw_readl(reg_base + 0x400);
	val |= (1 << (pin % 8));
	__raw_writel(val, reg_base + 0x400); /* direction output */
	__raw_writel(0, reg_base + (1 << (pin % 8 + 2))); /* output 0 */
}

static inline void ds28e10_master_release(u32 pin)
{
	u32 val;
	u32 reg_base = GPIO0_REG_BASE + pin / 8 * 0x10000;

	val = __raw_readl(reg_base + 0x400);
	val &= ~(1 << (pin % 8));
	__raw_writel(val, reg_base + 0x400);
}

static inline int ds28e10_master_sample(u32 pin)
{
	u32 reg_base = GPIO0_REG_BASE + pin / 8 * 0x10000;
	return __raw_readl(reg_base + (1 << (pin % 8 + 2))) ? 1 : 0;
}

static u8 ds28e10_read_bit(u8 pin)
{
	int result;

	/*
	 *  \       /``````````````````\
	 * tF\_tRL_/....................\__
	 */
	ds28e10_master_pulldown(pin);
	udelay(6);

	ds28e10_master_release(pin);
	udelay(9);

	result = ds28e10_master_sample(pin);
	udelay(55);

	return result;
}

static void ds28e10_write_bit(u8 pin, int bit)
{
	/*
	 *  \        /``````````````````\
	 * tF\_tW1L_/                    \__
	 *  \                      /````\
	 * tF\________tW0L________/      \__
	 */
	if (bit) {
		ds28e10_master_pulldown(pin);
		udelay(6);
		ds28e10_master_release(pin);
		udelay(64);
	} else {
		ds28e10_master_pulldown(pin);
		udelay(60);
		ds28e10_master_release(pin);
		udelay(10);
	}
}

static int ds28e10_reset(u32 pin)
{
	int result;

	/*
	 *  \               /```\XXXXXXXX/`````\
	 * tF\____tRSTL____/tPDH \XXXXXX/ tREC  \__
	 *                         tMSR
	 */
	ds28e10_master_pulldown(pin);
	udelay(480);

	ds28e10_master_release(pin);
	udelay(70);

	result = ds28e10_master_sample(pin);
	udelay(410);

	ds28e10_master_release(pin);
	return result;
}

static u8 ds28e10_read_8(u32 pin)
{
	int i;
	u8 res = 0;

	for (i = 0; i < 8; ++i)
		res |= (ds28e10_read_bit(pin) << i);

	return res;
}

static u32 ds28e10_read_block(u32 pin,
		u8 *buf, u32 len)
{
	int i;

	for (i = 0; i < len; ++i)
		buf[i] = ds28e10_read_8(pin);

	return len;
}

static void ds28e10_write_8(u32 pin, u8 val)
{
	int i;

	for (i = 0; i < 8; ++i)
		ds28e10_write_bit(pin, (val >> i) & 0x1);
}

static u32 ds28e10_write_block(u32 pin, const u8 *buf, u32 len)
{
	int i;

	for (i = 0; i < len; ++i)
		ds28e10_write_8(pin, buf[i]);

	return len;
}

static int ds28e10_reset_slave(u32 pin)
{
	int rval;

	rval = ds28e10_reset(pin);
	if (rval)
		return rval;

	ds28e10_write_8(pin, 0xcc);
	return 0;
}

static int ds28e10_read_romid(u32 pin, u8 id[8])
{
	int try = 0;
	int rval;

	do {
		rval = ds28e10_reset(pin);
		if (rval)
			continue;

		ds28e10_write_8(pin, 0x33);	/* send read-romid cmd */
		ds28e10_read_block(pin, id, 8);	/* read the 8-bytes romid */
		rval = crc8(id, 8) ? -222 : 0;
	} while (rval && (++try < DS28E10_RETRY_CN));

	return rval;
}

static int ds28e10_write_challenge(u32 pin, u8 *buf)
{
	int rval;
	u8 challenge[12];

	rval = ds28e10_reset_slave(pin);
	if (rval)
		return rval;

	ds28e10_write_8(pin, 0x0F);	/* write-challenge command */

	/* write 12-byte challenge and then read it back */
	ds28e10_write_block(pin, buf, 12);
	ds28e10_read_block(pin, challenge, 12);

	return memcmp(challenge, buf, 12);
}

static int ds28e10_read_mac(u32 pin, struct authentication_data *pdata)
{
	u8 buf[34] = {0};
	int rval;
	int try = 0;

	do {
		rval = ds28e10_write_challenge(pin, pdata->challenge);
		if (rval)
			continue;

		/* reset device to read authentication page */
		rval = ds28e10_reset_slave(pin);
		if (rval)
			continue;

		/* read authentication page 0000h */
		buf[0] = 0xa5;
		buf[1] = 0x00;
		buf[2] = 0x00;
		ds28e10_write_block(pin, buf, 3);

		/* get the 28-bytes OTP, 0xFF and CRC16 */
		ds28e10_read_block(pin, &buf[3], 28 + 3);
		if (crc16(0, buf, 34) != 0xb001) {
			rval = -222;
			continue;
		}

		memcpy(pdata->pagedata, &buf[3], 28);

		/* waiting for Tcsha */
		udelay(2000);

		/* get the 20-bytes MAC and CRC16 */
		ds28e10_read_block(pin, pdata->mac, 20 + 2);
		if (crc16(0, pdata->mac, 22) != 0xb001)
			rval = -222;
	} while (rval && ++try < DS28E10_RETRY_CN);

	return rval;
}

static int ds28e10_soft_reset(u32 pin)
{
	u8 buf[20] = {0x55, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff};


	ds28e10_reset_slave(pin);
	ds28e10_write_block(pin, buf, 7);
	ds28e10_read_block(pin, &buf[7], 2);
	if (crc16(0, buf, 9) != 0xb001)
		return -222;

	udelay(100 * 1000);	/* 100ms */
	udelay(100);	/* delay for flushing cache */

	return 0;
}

/*
 * ---------------------------------------------------
 * the mac generate routine
 * ---------------------------------------------------
 */
static u32 *generate_sha1(u8 *msg, u32 *sha1)
{
	int i;
	u32 tmp;
	u32 mt[80];
	u32 ktn[4] = {0x5a827999, 0x6ed9eba1, 0x8f1bbcdc, 0xca62c1d6};

	for (i = 0; i < 16; i++) {
		mt[i] = ((long)msg[i * 4] & 0xff) << 24
			| ((long)msg[i * 4 + 1] & 0xff) << 16
			| ((long)msg[i * 4 + 2] & 0xff) << 8
			| ((long)msg[i * 4 + 3] & 0xff);
	}

	for (i = 16; i < 80; i++) {
		tmp = mt[i - 3] ^ mt[i - 8] ^ mt[i - 14] ^ mt[i - 16];
		mt[i] = (tmp << 1 & 0xfffffffe) | (tmp >> 31 & 0x1);
	}

	sha1[0] = 0x67452301;
	sha1[1] = 0xefcdab89;
	sha1[2] = 0x98badcfe;
	sha1[3] = 0x10325476;
	sha1[4] = 0xc3d2e1f0;

	for (i = 0; i < 80; i++) {
		tmp = (sha1[0] << 5 & 0xffffffe0) | (sha1[0] >> 27 & 0x1f);

		if (i < 20)
			tmp += (sha1[1] & sha1[2]) | (~sha1[1] & sha1[3]);
		else if (i < 40)
			tmp += sha1[1] ^ sha1[2] ^ sha1[3];
		else if (i < 60)
			tmp += (sha1[1] & sha1[2])
				| (sha1[1] & sha1[3]) | (sha1[2] & sha1[3]);
		else
			tmp += sha1[1] ^ sha1[2] ^ sha1[3];

		tmp += sha1[4] + ktn[i / 20] + mt[i];
		sha1[4] = sha1[3];
		sha1[3] = sha1[2];
		sha1[2] = (sha1[1] << 30 & 0xc0000000)
			| (sha1[1] >> 2 & 0x3fffffff);
		sha1[1] = sha1[0];
		sha1[0] = tmp;
	}
	return sha1;
}

static u8 *generate_secret(u8 *romid, u8 *secret)
{
	int i;
	u32 sha1[5];
	u8 msg[64] = {
		0x5a, 0xa5, 0xff, 0x00, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
		/* the following line needs be changed */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x9e, 0x17, 0xd3, 0x88, 0xff, 0xff, 0xff, 0x80,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xb8
	};

	for (i = 0; i < 8; i++)
		msg[40 + i] = romid[i];

	msg[40] &= 0x3f;

	generate_sha1(msg, sha1);

	for (i = 0; i < 4; i++) {
		u32 tmp = sha1[4 - i];
		secret[i * 2 + 0] = tmp;
		tmp >>= 16;
		secret[i * 2 + 1] = tmp;
	}
	return secret;
}

static u8 *generate_mac(u8 *msg, u8 *mac)
{
	int i;
	u32 sha1[5];

	generate_sha1(msg, sha1);

	for (i = 0; i < 5; i++) {
		long tmp = sha1[4 - i];

		mac[i * 4 + 0] = tmp;
		tmp >>= 8;
		mac[i * 4 + 1] = tmp;
		tmp >>= 8;
		mac[i * 4 + 2] = tmp;
		tmp >>= 8;
		mac[i * 4 + 3] = tmp;
	}
	return mac;
}

static int authenticate(u32 pin, struct authentication_data *pdata, u8 *otp)
{
	int i;
	int rval;
	u8 romid[8];
	u8 secret[8];
	u8 msg[64];
	u8 mac[20];

	rval = ds28e10_read_romid(pin, romid);
	if (rval) {
		puts("auth: fails to read romid\n");
		return rval;
	}

	generate_secret(romid, secret);

	rval = ds28e10_read_mac(pin, pdata);
	if (rval) {
		puts("auth: fails to read mac\n");
		return rval;
	}

	for (i = 0; i < 4; i++)
		msg[i] = secret[i];
	for (i = 0; i < 28; i++)
		msg[4 + i] = pdata->pagedata[i];
	for (i = 0; i < 4; i++)
		msg[32 + i] = pdata->challenge[8 + i];
	for (i = 0; i < 4; i++)
		msg[36 + i] = pdata->challenge[i];
	for (i = 0; i < 7; i++)
		msg[41 + i] = romid[i];
	for (i = 0; i < 4; i++)
		msg[48 + i] = secret[4 + i];
	for (i = 0; i < 3; i++)
		msg[52 + i] = pdata->challenge[4 + i];
	for (i = 0; i < 6; i++)
		msg[56 + i] = 0x00;
	msg[40] = pdata->challenge[7];
	msg[55] = 0x80;
	msg[62] = 0x01;
	msg[63] = 0xb8;

	generate_mac(msg, mac);

	for (i = 0; i < 20; i++)
		if (mac[i] != pdata->mac[i])
			return -999;

	for (i = 0; i < 4; i++)
		otp[i] = pdata->pagedata[i];

	return 0;
}

int eth_set_hwaddr(u32 pin)
{
	struct authentication_data data;
	u8 p[6] = {0x5e, 0xa6, 0xff, 0xff, 0xff, 0xff};
	u8 sn[4];
	int rval;
	char ethaddr[20];

	rval = ds28e10_soft_reset(pin);
	if (rval) {
		puts("ds28e10_soft_reset fails\n");
		goto _out;
	}

	rval = authenticate(pin, &data, sn);
	if (!rval)
		memcpy(&p[2], sn, 4);

_out:
	/* check and configure hwaddr */
       	sprintf(ethaddr, "%02X:%02X:%02X:%02X:%02X:%02X",
		       	p[0], p[1], p[2], p[3], p[4], p[5]);

	setenv ("ethaddr", ethaddr);
	return rval;
}
