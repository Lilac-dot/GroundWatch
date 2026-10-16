#ifndef QMC5883L_H
#define QMC5883L_H
#include <stdint.h>

uint8_t mag_init(void);                          /* 1 if the chip answered */
uint8_t mag_read(int16_t *x, int16_t *y, int16_t *z);   /* 1 on success */

#endif
