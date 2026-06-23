#pragma once

#include <stdint.h>
#include "display.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RSSI spectrum visualizer for the Guition JC3248W535EN (320x480).
 *
 * Layout (portrait):
 *   y=  0..41  Header: "binary-mist"  active channel  mode label
 *   y= 42..241  2.4 GHz channel bars (channels 1-13)
 *   y=242..399  Recent AP list (last 6 SSIDs seen)
 *   y=400..479  Action bar: [DEAUTH] [BEACON] [SWEEP] [STOP]
 *
 * Touch:
 *   Tap a channel bar → sets the active attack channel
 *   Tap [DEAUTH]      → broadcast deauth on active channel (20 frames)
 *   Tap [BEACON]      → random-SSID beacon spam on active channel
 *   Tap [SWEEP]       → passive 2.4 GHz channel sweep (200ms dwell)
 *   Tap [STOP]        → stop all attacks
 */

#define SPECTRUM_CH24_COUNT   13   /* channels 1-13 */
#define SPECTRUM_DECAY_MS   3000   /* RSSI falls to background after 3 s no signal */

void spectrum_init(void);

/* Called from wifi_attack frame hook — ISR-safe (just writes atomic int8_t). */
void spectrum_update(uint8_t channel, int8_t rssi);

/* FreeRTOS task body.  arg must be a bm_display_t *.
 * Pin to core 1 at priority 4 (below CLI at 5).               */
void spectrum_task(void *arg);

#ifdef __cplusplus
}
#endif
