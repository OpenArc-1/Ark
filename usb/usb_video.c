/*
 * usb/usb_video.c — USB Video Class (UVC)
 *
 * UVC provides isochronous and bulk video streaming for webcams, capture
 * cards, and USB cameras.
 * Class code: 0x0E (Video), subclass 0x01 (Video Control) or 0x02 (Video Streaming)
 *
 * Interface structure (mirrors USB Audio in design):
 *   VideoControl interface (VC): describes camera capabilities, controls.
 *   VideoStreaming interface (VS): isochronous or bulk video payload.
 *
 * Key descriptor subtypes:
 *   VC_INPUT_TERMINAL  0x02 — camera sensor or composite input
 *   VC_OUTPUT_TERMINAL 0x03 — USB stream output
 *   VC_PROCESSING_UNIT 0x05 — brightness, contrast, hue, white balance
 *   VS_INPUT_HEADER    0x01 — streaming formats list
 *   VS_FORMAT_MJPEG    0x06 — Motion JPEG compressed format
 *   VS_FORMAT_UNCOMPRESSED 0x04 — raw YUY2/NV12 format descriptor
 *   VS_FRAME_*         0x07/0x05 — resolution + frame rate descriptor
 *
 * Probe/Commit negotiation:
 *   Host sends SET_CUR(VS_PROBE_CONTROL) with preferred format/frame/interval.
 *   Device returns adjusted values in GET_CUR(VS_PROBE_CONTROL).
 *   Host accepts with SET_CUR(VS_COMMIT_CONTROL).
 *   Host then selects non-zero alternate setting on VS interface to start streaming.
 *
 * Video payload header (prepended to each isochronous packet):
 *   Byte 0: bHeaderLength
 *   Byte 1: bmHeaderInfo
 *     bit 0: FID (Frame ID, toggles each new frame)
 *     bit 1: EOF (end of video frame)
 *     bit 2: PTS present
 *     bit 3: SCR present
 *     bit 6: error
 *     bit 7: EOH (end of header)
 *
 * Ark integration:
 *   uvc_init(dev)               — probe/commit format negotiation
 *   uvc_start_stream()          — select streaming alternate setting
 *   uvc_poll(frame_buf, maxsz)  — collect isochronous packets into frame
 *   uvc_set_brightness(val)     — processing unit control
 */
#include "ark/types.h"
#include "ark/printk.h"

void uvc_init(u8 addr) {
    printk(T, "[UVC] init addr=%u\n", addr);
}
