#ifndef I2C_H
#define I2C_H
#include <stdint.h>

/* Bit-banged I2C master on P3.4 (SCL) / P3.5 (SDA), roughly 50-100 kHz */
void    i2c_init(void);
void    i2c_start(void);
void    i2c_stop(void);
uint8_t i2c_write(uint8_t b);     /* returns 1 if the slave ACKed */
uint8_t i2c_read(uint8_t ack);    /* ack = 1 for every byte except the last */

#endif
