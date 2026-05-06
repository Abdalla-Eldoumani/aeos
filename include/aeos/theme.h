/* ============================================================================
 * AEOS - Abdalla's Educational Operating System
 * File: include/aeos/theme.h
 * Description: Single source of truth for the dark palette. Every literal
 *              color in src/ should resolve to one of these tokens.
 * ============================================================================ */

#ifndef AEOS_THEME_H
#define AEOS_THEME_H

/* Background and surfaces */
#define THEME_BG_DEEP            0xFF0E1116u  /* desktop base */
#define THEME_BG_GRADIENT_TOP    0xFF131822u  /* desktop gradient top */
#define THEME_BG_GRADIENT_BOT    0xFF0A0D14u  /* desktop gradient bottom */
#define THEME_SURFACE_1          0xFF1A1F2Bu  /* window client area */
#define THEME_SURFACE_2          0xFF222836u  /* section panels inside windows */
#define THEME_SURFACE_3          0xFF2A3142u  /* hover, taskbar buttons */

/* Borders */
#define THEME_BORDER_SUBTLE      0xFF2C3344u  /* unfocused window border */
#define THEME_BORDER_STRONG      0xFF3D4659u  /* focused window border */

/* Accent and focus */
#define THEME_ACCENT             0xFF7AA2F7u  /* focused title bar, primary */
#define THEME_ACCENT_DIM         0xFF3F5984u  /* disabled accent state */
#define THEME_ACCENT_GLOW        0x807AA2F7u  /* focus ring, drop shadow */

/* Text */
#define THEME_TEXT_PRIMARY       0xFFE6E9EFu  /* body text */
#define THEME_TEXT_SECONDARY     0xFF9BA3B5u  /* labels, metadata */
#define THEME_TEXT_MUTED         0xFF5C6478u  /* placeholder, footer */

/* Status */
#define THEME_SUCCESS            0xFF7CC78Fu  /* terminal stdout, ok */
#define THEME_WARNING            0xFFE0AF68u  /* warnings, modified marker */
#define THEME_DANGER             0xFFE56270u  /* errors, close button */

#endif /* AEOS_THEME_H */

/* ============================================================================
 * End of theme.h
 * ============================================================================ */
