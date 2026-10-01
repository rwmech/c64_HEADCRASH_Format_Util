/* ui.h - hi-res bitmap interface for HEADCRASH Format Util.
 *
 * 320x200 mono bitmap, one ink per 8x8 cell, cyan line art on black, which
 * is as close as a C64 gets to the thin stroke drawings the project uses
 * elsewhere.
 *
 * (C) 2026 Robert Mech. Licence GPL-3.0-or-later.
 */

#ifndef UI_H
#define UI_H

/* C64 colour numbers used by the interface. */
#define UI_BLACK  0u
#define UI_WHITE  1u
#define UI_RED    2u
#define UI_CYAN   3u
#define UI_GREY   12u
#define UI_LGREEN 13u
#define UI_LBLUE  14u
#define UI_LGREY  15u
#define UI_DGREY  11u
#define UI_LRED   10u
#define UI_YELLOW 7u

/* Bring up the bitmap, clear it, copy the character set out of ROM. */
void ui_init(void);

/* Put the machine back the way it was found, for a clean exit to BASIC. */
void ui_done(void);

/* --- drawing ---------------------------------------------------------- */

void ui_clear(void);

/* Ink for everything drawn after this call, applied per 8x8 cell as it is
 * touched. Two inks cannot share a cell, so the layout keeps them apart.
 */
void ui_ink(unsigned char colour);

/* x runs 0..319, so it does not fit in a byte. y does. */
void ui_plot(unsigned int x, unsigned char y);
void ui_hline(unsigned int x1, unsigned int x2, unsigned char y);
void ui_vline(unsigned int x, unsigned char y1, unsigned char y2);
void ui_box(unsigned int x, unsigned char y,
            unsigned int w, unsigned char h);
void ui_fill(unsigned int x, unsigned char y,
             unsigned int w, unsigned char h);
void ui_circle(unsigned int cx, unsigned char cy, unsigned char r);

/* Text, positioned by pixel so it can sit next to line art. Strings are
 * plain C literals; the glyphs come from the uppercase/graphics set.
 */
void ui_text(unsigned int x, unsigned char y, const char *s);
void ui_text_pad(unsigned int x, unsigned char y, const char *s,
                 unsigned char width);
void ui_num(unsigned int x, unsigned char y, unsigned int n,
            unsigned char digits);

#endif /* UI_H */
