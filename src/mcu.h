/*
 * GroundWatch - compiler compatibility
 *
 * The firmware is written with SDCC keywords (__bit, __idata, __interrupt(n)...)
 * and SDCC pin names (P0_3). This header lets the same files build in:
 *   - SDCC        (make, VS Code)
 *   - Keil C51    (uVision, the lab PCs)
 *   - the host C compiler (make test)
 * Include it instead of <stdint.h> or the chip header.
 */
#ifndef MCU_H
#define MCU_H

#if defined(__SDCC)
/* ---------------- SDCC ---------------- */
#include <8052.h>
#include <stdint.h>

#elif defined(__C51__)
/* ---------------- Keil C51 ---------------- */
#include <reg52.h>

/* C51 has no <stdint.h> (int is 16-bit, long is 32-bit, like SDCC) */
typedef unsigned char  uint8_t;
typedef signed char    int8_t;
typedef unsigned int   uint16_t;
typedef signed int     int16_t;
typedef unsigned long  uint32_t;
typedef signed long    int32_t;

/* SDCC keywords -> Keil keywords */
#define __data          data
#define __idata         idata
#define __code          code
#define __bit           bit
#define __interrupt(n)  interrupt n
#define __using(n)      using n

/* SDCC's <8052.h> names single port pins P0_0 ... ; Keil's <reg52.h> doesn't,
 * so declare the ones config.h uses. */
sbit P0_0 = P0^0;
sbit P0_1 = P0^1;
sbit P0_2 = P0^2;
sbit P0_3 = P0^3;
sbit P0_4 = P0^4;
sbit P0_5 = P0^5;
sbit P2_0 = P2^0;
sbit P2_1 = P2^1;
sbit P2_2 = P2^2;
sbit P2_3 = P2^3;
sbit P2_4 = P2^4;
sbit P2_5 = P2^5;
sbit P2_6 = P2^6;
sbit P2_7 = P2^7;
sbit P3_2 = P3^2;
sbit P3_3 = P3^3;

#else
/* ---------------- host build (make test) ---------------- */
#include <stdint.h>
#ifndef HOST_8051_H     /* tests/host_8051.h already defines these */
#define __data
#define __idata
#define __code
#define __bit           uint8_t
#define __interrupt(n)
#define __using(n)
#endif

#endif

#endif
