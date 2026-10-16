/* Bit-banged I2C. 8051 pins are quasi-bidirectional: writing 1 releases the line. */
#include "config.h"
#include "i2c.h"

static void dly(void)
{
    __asm
        nop
        nop
        nop
        nop
    __endasm;
}

void i2c_init(void)
{
    I2C_SDA = 1;
    I2C_SCL = 1;
}

void i2c_start(void)
{
    I2C_SDA = 1;
    I2C_SCL = 1; dly();
    I2C_SDA = 0; dly();
    I2C_SCL = 0;
}

void i2c_stop(void)
{
    I2C_SDA = 0;
    I2C_SCL = 1; dly();
    I2C_SDA = 1; dly();
}

uint8_t i2c_write(uint8_t b)
{
    uint8_t i, ack;

    for (i = 0; i < 8; i++) {
        I2C_SDA = (b & 0x80) ? 1 : 0;
        I2C_SCL = 1; dly();
        I2C_SCL = 0;
        b <<= 1;
    }
    I2C_SDA = 1;                          /* release for ACK */
    I2C_SCL = 1; dly();
    ack = I2C_SDA ? 0 : 1;
    I2C_SCL = 0;
    return ack;
}

uint8_t i2c_read(uint8_t ack)
{
    uint8_t i, v = 0;

    I2C_SDA = 1;
    for (i = 0; i < 8; i++) {
        v <<= 1;
        I2C_SCL = 1; dly();
        if (I2C_SDA) v |= 1;
        I2C_SCL = 0;
    }
    I2C_SDA = ack ? 0 : 1;
    I2C_SCL = 1; dly();
    I2C_SCL = 0;
    I2C_SDA = 1;
    return v;
}
