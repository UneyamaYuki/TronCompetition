#include "collector_protocol.h"
#include "collector_state.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    collector_context_t context;
    collector_state_initialize(&context);
    assert(context.state == COLLECTOR_STATE_WAITING);
    assert(!collector_state_start(&context, 1000U, 7U));

    collector_state_set_usb_ready(&context, true);
    assert(collector_state_start(&context, 1000U, 7U));
    assert(collector_state_timestamp(&context, 1250U) == 250U);
    assert(collector_state_feed_marker(&context));
    assert(context.statistics.feed_markers == 1U);
    assert(collector_state_next_sequence(&context) == 0U);
    assert(collector_state_next_sequence(&context) == 1U);
    assert(collector_state_stop(&context));
    collector_state_stopped(&context);
    assert(context.state == COLLECTOR_STATE_WAITING);

    uint8_t payload[] = {0xFFU, 0xD8U, 0x01U, 0x02U, 0xFFU, 0xD9U};
    collector_record_header_t header;
    collector_protocol_header_build(&header, COLLECTOR_RECORD_JPEG_FRAME, 7U, 2U, 250U,
                                    payload, (uint32_t) sizeof(payload));
    assert(collector_protocol_header_valid(&header));
    assert(header.payload_crc32 == collector_crc32(payload, sizeof(payload)));

    collector_record_header_t corrupted;
    memcpy(&corrupted, &header, sizeof(corrupted));
    corrupted.sequence++;
    assert(!collector_protocol_header_valid(&corrupted));
    return 0;
}