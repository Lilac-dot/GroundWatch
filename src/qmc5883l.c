/* QMC5883L 3-axis magnetometer (GY-271 module), I2C address 0x0D */
#include "config.h"
#include "i2c.h"
#include "qmc5883l.h"

#define QMC_W       (0x0D << 1)
#define QMC_R       ((0x0D << 1) | 1)
#define REG_DATA    0x00            /* X LSB, X MSB, Y LSB, Y MSB, Z LSB, Z MSB */
#define REG_CTRL1   0x09
#define REG_SETRST  0x0B

static uint8_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t ok;
    i2c_start();
    ok = i2c_write(QMC_W);
    ok = ok && i2c_write(reg);
    ok = ok && i2c_write(val);
    i2c_stop();
    return ok;
}

uint8_t mag_init(void)
{
    i2c_init();
    if (!write_reg(REG_SETRST, 0x01)) return 0;
    /* OSR 512, range 8 G, 200 Hz, continuous mode */
    return write_reg(REG_CTRL1, 0x1D);
}

uint8_t mag_read(int16_t *x, int16_t *y, int16_t *z)
{
    __idata uint8_t b[6];
    uint8_t i;

    i2c_start();
    if (!i2c_write(QMC_W) || !i2c_write(REG_DATA)) { i2c_stop(); return 0; }
    i2c_start();                           /* repeated start */
    if (!i2c_write(QMC_R)) { i2c_stop(); return 0; }
    for (i = 0; i < 6; i++) b[i] = i2c_read(i < 5);
    i2c_stop();

    *x = (int16_t)((uint16_t)b[1] << 8 | b[0]);
    *y = (int16_t)((uint16_t)b[3] << 8 | b[2]);
    *z = (int16_t)((uint16_t)b[5] << 8 | b[4]);
    return 1;
}
