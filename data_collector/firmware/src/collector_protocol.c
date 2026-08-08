#include "collector_protocol.h"

#include <string.h>

_Static_assert(sizeof(collector_record_header_t) == COLLECTOR_PROTOCOL_HEADER_SIZE,
               "collector protocol header layout changed");
_Static_assert(sizeof(collector_frame_metadata_t) == 16U,
               "JPEG payload must remain 8-byte aligned");

uint32_t collector_crc32(const void * p_data, size_t size)
{
    uint8_t const * p_bytes = (uint8_t const *) p_data;
    uint32_t crc = UINT32_MAX;

    for (size_t index = 0; index < size; index++)
    {
        crc ^= p_bytes[index];
        for (uint32_t bit = 0; bit < 8U; bit++)
        {
            uint32_t mask = (uint32_t) (-(int32_t) (crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

void collector_protocol_header_build(collector_record_header_t * p_header,
                                     collector_record_type_t type,
                                     uint32_t session_id,
                                     uint32_t sequence,
                                     uint32_t timestamp_ms,
                                     const void * p_payload,
                                     uint32_t payload_size)
{
    memset(p_header, 0, sizeof(*p_header));
    p_header->magic         = COLLECTOR_PROTOCOL_MAGIC;
    p_header->version       = COLLECTOR_PROTOCOL_VERSION;
    p_header->type          = (uint8_t) type;
    p_header->header_size   = COLLECTOR_PROTOCOL_HEADER_SIZE;
    p_header->session_id    = session_id;
    p_header->sequence      = sequence;
    p_header->timestamp_ms  = timestamp_ms;
    p_header->payload_size  = payload_size;
    p_header->payload_crc32 = collector_crc32(p_payload, payload_size);
    p_header->header_crc32  = collector_crc32(p_header, offsetof(collector_record_header_t, header_crc32));
}

bool collector_protocol_header_valid(const collector_record_header_t * p_header)
{
    if ((p_header->magic != COLLECTOR_PROTOCOL_MAGIC) ||
        (p_header->version != COLLECTOR_PROTOCOL_VERSION) ||
        (p_header->header_size != COLLECTOR_PROTOCOL_HEADER_SIZE))
    {
        return false;
    }

    uint32_t actual_crc = collector_crc32(p_header, offsetof(collector_record_header_t, header_crc32));
    return actual_crc == p_header->header_crc32;
}