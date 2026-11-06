#include "library/segment_stream.h"

static bool read_committed(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    SegmentStreamIo *io = context;
    return segment_store_read(io->store, io->stream, offset, buffer, length);
}

static bool write_working(void *context, uint32_t offset, const void *bytes, uint32_t length)
{
    SegmentStreamIo *io = context;
    uint32_t end = segment_store_working_length(io->store, io->stream);
    if (offset == end) {
        io->result = segment_store_append(io->store, io->stream, bytes, length);
    } else if (offset < end && length <= end - offset) {
        io->result = segment_store_write(io->store, io->stream, offset, bytes, length);
    } else {
        io->result = SEGMENT_REFUSED;
    }
    return io->result == SEGMENT_OK;
}

ByteSource segment_stream_source(SegmentStreamIo *io)
{
    return (ByteSource){.read = read_committed, .context = io};
}

ByteSink segment_stream_sink(SegmentStreamIo *io)
{
    io->result = SEGMENT_OK;
    return (ByteSink){.write = write_working, .context = io};
}
