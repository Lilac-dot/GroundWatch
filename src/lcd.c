/*
 * HD44780 16x2 LCD, 4-bit mode on P2 (RS=P2.0, EN=P2.1, D4..D7=P2.4..P2.7).
 * Bit writes only: the buzzer and LED share P2, and a byte write would read
 * the pins back (a transistor base reads as 0) and switch the buzzer off.
 */
#include "config.h"
#include "lcd.h"
#include "sense.h"

static void short_delay(void)            /* ~50 us at 11.0592 MHz */
{
    volatile uint8_t i;
    for (i = 0; i < 12; i++) ;
}

static void lcd_nibble(uint8_t n)        /* sends high nibble of n */
{
    LCD_D4 = (n & 0x10) ? 1 : 0;
    LCD_D5 = (n & 0x20) ? 1 : 0;
    LCD_D6 = (n & 0x40) ? 1 : 0;
    LCD_D7 = (n & 0x80) ? 1 : 0;
    LCD_EN = 1;
    short_delay();
    LCD_EN = 0;
    short_delay();
}

static void lcd_write(uint8_t b, uint8_t rs)
{
    LCD_RS = rs ? 1 : 0;
    lcd_nibble(b);
    lcd_nibble(b << 4);
}

static void lcd_cmd(uint8_t c)
{
    lcd_write(c, 0);
    if (c <= 0x03) delay_ms(2);          /* clear / home are slow */
}

void lcd_init(void)
{
    LCD_EN = 0;
    LCD_RS = 0;
    delay_ms(40);
    lcd_nibble(0x30); delay_ms(5);
    lcd_nibble(0x30); delay_ms(1);
    lcd_nibble(0x30); delay_ms(1);
    lcd_nibble(0x20); delay_ms(1);       /* switch to 4-bit */
    lcd_cmd(0x28);                       /* 4-bit, 2 lines, 5x8 */
    lcd_cmd(0x0C);                       /* display on, cursor off */
    lcd_cmd(0x06);                       /* auto-increment */
    lcd_cmd(0x01);                       /* clear */
}

void lcd_goto(uint8_t row, uint8_t col)  { lcd_cmd((row ? 0xC0 : 0x80) + col); }
void lcd_putc(char c)                    { lcd_write((uint8_t)c, 1); }

void lcd_print_line(uint8_t row, const char *s)
{
    uint8_t n = 0;
    lcd_goto(row, 0);
    while (*s && n < 16) { lcd_putc(*s++); n++; }
    while (n < 16)       { lcd_putc(' ');  n++; }
}
