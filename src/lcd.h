#ifndef LCD_H
#define LCD_H
#include <stdint.h>

void lcd_init(void);                     /* needs the 1 ms tick running */
void lcd_goto(uint8_t row, uint8_t col);
void lcd_putc(char c);
void lcd_print_line(uint8_t row, const char *s);  /* writes and pads to 16 chars */

#endif
