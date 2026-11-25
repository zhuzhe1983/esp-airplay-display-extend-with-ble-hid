#pragma once
#define CONFIG_P4_STREAM_WIDTH 640
#define CONFIG_P4_STREAM_HEIGHT 360
#define CONFIG_P4_STREAM_FPS 10
/* Deliberately small so queue-overflow interleavings need only small buffers. */
#define CONFIG_P4_FRAME_QUEUE_BYTES 64
#define CONFIG_P4_INPUT_ABSOLUTE 1
#define CONFIG_FREERTOS_UNICORE 0
