/*
 * usb/usb_audio.c — USB Audio Device Class (UAC)
 *
 * USB Audio provides isochronous audio streaming for speakers, microphones,
 * headsets, and USB sound cards.
 * Class code: 0x01 (Audio), subclass 0x01 (Audio Control) or 0x02 (Audio Streaming)
 *
 * Interface structure:
 *   Interface 0 — Audio Control (AC): describes the audio topology (terminals,
 *     units, selectors). Uses only the default control pipe.
 *   Interface 1 — Audio Streaming OUT (AS): PCM data from host → speaker.
 *     Isochronous OUT endpoint. Alternate setting 0 = zero bandwidth (idle).
 *   Interface 2 — Audio Streaming IN  (AS): PCM data microphone → host.
 *     Isochronous IN endpoint.
 *
 * UAC1 vs UAC2:
 *   UAC1 (USB Audio Class 1.0): max 96 kHz/24-bit, no explicit feedback.
 *   UAC2 (USB Audio Class 2.0): supports 192 kHz/32-bit, high-speed only,
 *     explicit/implicit feedback endpoints for clock synchronization.
 *
 * Audio Control descriptors (bDescriptorSubtype):
 *   0x01 HEADER         — AC interface header, lists streaming interfaces
 *   0x02 INPUT_TERMINAL — audio source (USB stream or physical mic)
 *   0x03 OUTPUT_TERMINAL — audio sink (USB stream or speaker)
 *   0x06 FEATURE_UNIT   — volume, mute, bass, treble controls
 *   0x08 SELECTOR_UNIT  — audio path selector (switch between sources)
 *
 * Control requests (bmRequestType = 0x21 or 0xA1):
 *   SET_CUR  0x01 — set current value (volume, mute)
 *   GET_CUR  0x81 — read current value
 *   GET_MIN  0x82 — minimum value
 *   GET_MAX  0x83 — maximum value
 *   GET_RES  0x84 — resolution (step size)
 *
 * Volume control: wValue = (feature unit ID << 8) | channel
 *   Channel 0 = master, 1 = left, 2 = right
 *   Volume in 1/256 dB units, signed 16-bit
 *
 * Isochronous transfer (audio streaming):
 *   Each USB frame (1ms full-speed, 125µs high-speed) carries one packet.
 *   Packet size = sample_rate / 1000 * channels * bytes_per_sample
 *   Example: 48kHz stereo 16-bit = 48 * 2 * 2 = 192 bytes/ms
 *
 * Ark integration:
 *   usb_audio_init(dev)           — parse AC descriptor, select streaming alt
 *   usb_audio_set_volume(pct)     — set master volume 0-100%
 *   usb_audio_set_mute(mute)      — mute/unmute
 *   usb_audio_write(pcm, frames)  — queue isochronous OUT transfer
 *   usb_audio_read(pcm, frames)   — read isochronous IN transfer (microphone)
 */
#include "ark/types.h"
#include "ark/printk.h"

void usb_audio_init(u8 addr) {
    printk(T, "[USB-Audio] init addr=%u\n", addr);
}

void usb_audio_set_volume(u8 addr, u8 pct) {
    printk(T, "[USB-Audio] addr=%u volume=%u%%\n", addr, pct);
}
