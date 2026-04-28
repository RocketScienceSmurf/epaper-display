#pragma once

// URL of the BMP image to display (must be 200x200, 24bpp uncompressed BMP)
#define IMAGE_URL "http://192.168.8.8:10000/lovelace/200x200?viewport=200x200&format=bmp&lang=en"

// How often to refresh the display (ms). e-paper has limited refresh cycles,
// so don't set this too low. 5 minutes is a reasonable minimum.
#define REFRESH_INTERVAL_MS (15UL * 60UL * 1000UL)

// Ethernet timeout on boot (ms)
#define ETH_CONNECT_TIMEOUT_MS 30000

// Color thresholds for converting 24bpp BMP to B/W/R
// Pixels with R>RED_MIN and G<RED_MAX and B<RED_MAX are treated as red.
// Pixels with R<DARK_MAX and G<DARK_MAX and B<DARK_MAX are treated as black.
// Everything else is white.
#define COLOR_DARK_MAX  85
#define COLOR_RED_MIN  170
#define COLOR_RED_MAX   85
