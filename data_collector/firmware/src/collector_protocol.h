#ifndef COLLECTOR_PROTOCOL_H
#define COLLECTOR_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COLLECTOR_PROTOCOL_MAGIC        (0x43445141UL)
#define COLLECTOR_PROTOCOL_VERSION      (1U)
#define COLLECTOR_PROTOCOL_HEADER_SIZE  (32U)

typedef enum e_collector_record_type
{
    COLLECTOR_RECORD_SESSION_START = 1,
    COLLECTOR_RECORD_JPEG_FRAME    = 2,
    COLLECTOR_RECORD_FEED_MARKER   = 3,
    COLLECTOR_RECORD_ERROR         = 4,
    COLLECTOR_RECORD_SESSION_END   = 5
} collector_record_type_t;

typedef struct st_collector_record_header
{
    uint32_t magic;
    uint8_t  version;
    uint8_t  type;
    uint16_t header_size;
    uint32_t session_id;
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint32_t payload_size;
    uint32_t payload_crc32;
    uint32_t header_crc32;
} collector_record_header_t;

typedef struct st_collector_frame_metadata
{
    uint16_t width;
    uint16_t height;
    uint8_t  quality;
    uint8_t  reserved[3];
    uint32_t encode_time_us;
    uint32_t reserved2;
} collector_frame_metadata_t;

uint32_t collector_crc32(const void * p_data, size_t size);
void collector_protocol_header_build(collector_record_header_t * p_header,
                                     collector_record_type_t type,
                                     uint32_t session_id,
                                     uint32_t sequence,
                                     uint32_t timestamp_ms,
                                     const void * p_payload,
                                     uint32_t payload_size);
bool collector_protocol_header_valid(const collector_record_header_t * p_header);

#endif