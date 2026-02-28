///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Audio Stream Metadata Extraction
//
// Implements metadata parsing for various audio formats.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// C11 nullptr compatibility
#ifndef nullptr
#define nullptr ((void*)0)
#endif

#include "metadata.h"

#include <ctype.h>
#include <string.h>

// Platform-compatible case-insensitive string compare
#ifdef _WIN32
#define strncasecmp _strnicmp
#else
#include <strings.h> // For strncasecmp on POSIX
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: safely copy string with length limit and null termination

static void safe_strcpy(char* dest, size_t dest_size, const char* src, size_t src_len) {
    if (dest == nullptr || dest_size == 0) {
        return;
    }
    size_t copy_len = src_len;
    if (copy_len >= dest_size) {
        copy_len = dest_size - 1;
    }
    if (src != nullptr && copy_len > 0) {
        memcpy(dest, src, copy_len);
    }
    dest[copy_len] = '\0';

    // Trim trailing whitespace
    while (copy_len > 0 && (dest[copy_len - 1] == ' ' || dest[copy_len - 1] == '\0')) {
        dest[--copy_len] = '\0';
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: read 32-bit big-endian integer

static uint32_t read_be32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: read 32-bit little-endian integer

static uint32_t read_le32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper: read ID3v2 syncsafe integer (7 bits per byte)

static uint32_t read_syncsafe(const uint8_t* data) {
    return ((uint32_t)(data[0] & 0x7F) << 21) | ((uint32_t)(data[1] & 0x7F) << 14) | ((uint32_t)(data[2] & 0x7F) << 7)
           | (uint32_t)(data[3] & 0x7F);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ID3v1 parsing (last 128 bytes of file)

static bool parse_id3v1(const uint8_t* data, uint64_t size, AudioMetadata* metadata) {
    if (size < 128) {
        return false;
    }

    const uint8_t* tag = data + size - 128;

    // Check for "TAG" marker
    if (tag[0] != 'T' || tag[1] != 'A' || tag[2] != 'G') {
        return false;
    }

    // ID3v1 layout:
    // 0-2: "TAG"
    // 3-32: Title (30 bytes)
    // 33-62: Artist (30 bytes)
    // 63-92: Album (30 bytes)
    // 93-96: Year (4 bytes)
    // 97-126: Comment (30 bytes, or 28 + track in ID3v1.1)
    // 127: Genre (1 byte index)

    safe_strcpy(metadata->title, sizeof(metadata->title), (const char*)&tag[3], 30);
    safe_strcpy(metadata->artist, sizeof(metadata->artist), (const char*)&tag[33], 30);
    safe_strcpy(metadata->album, sizeof(metadata->album), (const char*)&tag[63], 30);
    safe_strcpy(metadata->date, sizeof(metadata->date), (const char*)&tag[93], 4);

    // Genre is an index - we'll leave it empty for now as decoding requires a genre table
    metadata->genre[0] = '\0';

    metadata->has_metadata = (metadata->title[0] != '\0' || metadata->artist[0] != '\0');
    return metadata->has_metadata;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ID3v2 frame parsing

static void parse_id3v2_frame(const char* frame_id, const uint8_t* frame_data, uint32_t frame_size,
                              AudioMetadata* metadata) {
    if (frame_size == 0) {
        return;
    }

    // Skip encoding byte for text frames
    const uint8_t* text_data = frame_data;
    uint32_t text_size = frame_size;

    if (frame_id[0] == 'T' && frame_size > 0) {
        // Text encoding: 0 = ISO-8859-1, 1 = UTF-16, 2 = UTF-16BE, 3 = UTF-8
        uint8_t encoding = frame_data[0];
        text_data = frame_data + 1;
        text_size = frame_size - 1;

        // For simplicity, only handle ISO-8859-1 and UTF-8
        if (encoding != 0 && encoding != 3) {
            // Skip UTF-16 for now
            return;
        }
    }

    // Match frame IDs (ID3v2.3/2.4 uses 4-char IDs)
    if (memcmp(frame_id, "TIT2", 4) == 0) {
        safe_strcpy(metadata->title, sizeof(metadata->title), (const char*)text_data, text_size);
    } else if (memcmp(frame_id, "TPE1", 4) == 0) {
        safe_strcpy(metadata->artist, sizeof(metadata->artist), (const char*)text_data, text_size);
    } else if (memcmp(frame_id, "TALB", 4) == 0) {
        safe_strcpy(metadata->album, sizeof(metadata->album), (const char*)text_data, text_size);
    } else if (memcmp(frame_id, "TYER", 4) == 0 || memcmp(frame_id, "TDRC", 4) == 0) {
        safe_strcpy(metadata->date, sizeof(metadata->date), (const char*)text_data, text_size);
    } else if (memcmp(frame_id, "TCON", 4) == 0) {
        safe_strcpy(metadata->genre, sizeof(metadata->genre), (const char*)text_data, text_size);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ID3v2 parsing

static bool parse_id3v2(const uint8_t* data, uint64_t size, AudioMetadata* metadata) {
    if (size < 10) {
        return false;
    }

    // Check for "ID3" marker
    if (data[0] != 'I' || data[1] != 'D' || data[2] != '3') {
        return false;
    }

    uint8_t version_major = data[3];
    uint8_t flags = data[5];
    uint32_t tag_size = read_syncsafe(&data[6]);

    // We support ID3v2.3 and ID3v2.4
    if (version_major < 3 || version_major > 4) {
        return false;
    }

    // Check if we have enough data
    if (size < 10 + tag_size) {
        return false;
    }

    // Skip extended header if present
    uint32_t offset = 10;
    if (flags & 0x40) {
        if (offset + 4 > 10 + tag_size) {
            return false;
        }
        uint32_t ext_size = read_syncsafe(&data[offset]);
        offset += 4 + ext_size;
    }

    // Parse frames
    while (offset + 10 <= 10 + tag_size) {
        const char* frame_id = (const char*)&data[offset];

        // Check for padding (all zeros)
        if (frame_id[0] == '\0') {
            break;
        }

        uint32_t frame_size;
        if (version_major == 4) {
            frame_size = read_syncsafe(&data[offset + 4]);
        } else {
            frame_size = read_be32(&data[offset + 4]);
        }

        // uint16_t frame_flags = (data[offset + 8] << 8) | data[offset + 9];
        offset += 10;

        if (offset + frame_size > 10 + tag_size) {
            break;
        }

        parse_id3v2_frame(frame_id, &data[offset], frame_size, metadata);
        offset += frame_size;
    }

    metadata->has_metadata = (metadata->title[0] != '\0' || metadata->artist[0] != '\0');
    return metadata->has_metadata;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool metadata_extract_id3(const uint8_t* data, uint64_t size, AudioMetadata* metadata) {
    if (data == nullptr || metadata == nullptr) {
        return false;
    }

    memset(metadata, 0, sizeof(AudioMetadata));

    // Try ID3v2 first (at start of file)
    if (parse_id3v2(data, size, metadata)) {
        return true;
    }

    // Fall back to ID3v1 (at end of file)
    return parse_id3v1(data, size, metadata);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vorbis comment parsing helper

static void parse_vorbis_comment(const char* comment, uint32_t length, AudioMetadata* metadata) {
    // Find the '=' separator
    const char* eq = memchr(comment, '=', length);
    if (eq == nullptr) {
        return;
    }

    size_t key_len = (size_t)(eq - comment);
    const char* value = eq + 1;
    size_t value_len = length - key_len - 1;

    // Case-insensitive key comparison
    if (key_len == 5 && strncasecmp(comment, "TITLE", 5) == 0) {
        safe_strcpy(metadata->title, sizeof(metadata->title), value, value_len);
    } else if (key_len == 6 && strncasecmp(comment, "ARTIST", 6) == 0) {
        safe_strcpy(metadata->artist, sizeof(metadata->artist), value, value_len);
    } else if (key_len == 5 && strncasecmp(comment, "ALBUM", 5) == 0) {
        safe_strcpy(metadata->album, sizeof(metadata->album), value, value_len);
    } else if (key_len == 4 && strncasecmp(comment, "DATE", 4) == 0) {
        safe_strcpy(metadata->date, sizeof(metadata->date), value, value_len);
    } else if (key_len == 5 && strncasecmp(comment, "GENRE", 5) == 0) {
        safe_strcpy(metadata->genre, sizeof(metadata->genre), value, value_len);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool metadata_extract_vorbis_comments(const uint8_t* data, uint64_t size, AudioMetadata* metadata) {
    if (data == nullptr || metadata == nullptr || size < 4) {
        return false;
    }

    memset(metadata, 0, sizeof(AudioMetadata));

    // Use stb_vorbis to extract comments
    // For now, we need to include stb_vorbis - but it's already compiled in decoder_vorbis.c
    // We'll use a different approach: parse the OGG pages directly

    // OGG Vorbis structure:
    // Page 1: Identification header
    // Page 2: Comment header (contains Vorbis comments)
    // Page 3+: Audio data

    // Check OGG magic
    if (data[0] != 'O' || data[1] != 'g' || data[2] != 'g' || data[3] != 'S') {
        return false;
    }

    // Find the comment header - scan for Vorbis comment packet marker
    // Vorbis comment header starts with 0x03 + "vorbis"
    const uint8_t vorbis_comment_marker[] = { 0x03, 'v', 'o', 'r', 'b', 'i', 's' };

    for (uint64_t i = 0; i + sizeof(vorbis_comment_marker) + 8 < size; i++) {
        if (memcmp(&data[i], vorbis_comment_marker, sizeof(vorbis_comment_marker)) == 0) {
            const uint8_t* comment_data = &data[i + sizeof(vorbis_comment_marker)];
            uint64_t remaining = size - i - sizeof(vorbis_comment_marker);

            if (remaining < 4) {
                return false;
            }

            // Read vendor string length (little-endian)
            uint32_t vendor_len = read_le32(comment_data);
            comment_data += 4;
            remaining -= 4;

            if (vendor_len > remaining) {
                return false;
            }

            // Skip vendor string
            comment_data += vendor_len;
            remaining -= vendor_len;

            if (remaining < 4) {
                return false;
            }

            // Read comment count
            uint32_t comment_count = read_le32(comment_data);
            comment_data += 4;
            remaining -= 4;

            // Parse each comment
            for (uint32_t j = 0; j < comment_count && remaining >= 4; j++) {
                uint32_t comment_len = read_le32(comment_data);
                comment_data += 4;
                remaining -= 4;

                if (comment_len > remaining) {
                    break;
                }

                parse_vorbis_comment((const char*)comment_data, comment_len, metadata);
                comment_data += comment_len;
                remaining -= comment_len;
            }

            metadata->has_metadata = (metadata->title[0] != '\0' || metadata->artist[0] != '\0');
            return metadata->has_metadata;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool metadata_extract_flac_comments(const uint8_t* data, uint64_t size, AudioMetadata* metadata) {
    if (data == nullptr || metadata == nullptr || size < 8) {
        return false;
    }

    memset(metadata, 0, sizeof(AudioMetadata));

    // Check FLAC magic
    if (data[0] != 'f' || data[1] != 'L' || data[2] != 'a' || data[3] != 'C') {
        return false;
    }

    // Parse metadata blocks
    uint64_t offset = 4;

    while (offset + 4 <= size) {
        uint8_t block_header = data[offset];
        bool is_last = (block_header & 0x80) != 0;
        uint8_t block_type = block_header & 0x7F;

        uint32_t block_size
            = ((uint32_t)data[offset + 1] << 16) | ((uint32_t)data[offset + 2] << 8) | (uint32_t)data[offset + 3];

        offset += 4;

        if (offset + block_size > size) {
            break;
        }

        // Block type 4 = VORBIS_COMMENT
        if (block_type == 4) {
            const uint8_t* comment_data = &data[offset];
            uint64_t remaining = block_size;

            if (remaining < 4) {
                break;
            }

            // Read vendor string length (little-endian)
            uint32_t vendor_len = read_le32(comment_data);
            comment_data += 4;
            remaining -= 4;

            if (vendor_len > remaining) {
                break;
            }

            // Skip vendor string
            comment_data += vendor_len;
            remaining -= vendor_len;

            if (remaining < 4) {
                break;
            }

            // Read comment count
            uint32_t comment_count = read_le32(comment_data);
            comment_data += 4;
            remaining -= 4;

            // Parse each comment
            for (uint32_t j = 0; j < comment_count && remaining >= 4; j++) {
                uint32_t comment_len = read_le32(comment_data);
                comment_data += 4;
                remaining -= 4;

                if (comment_len > remaining) {
                    break;
                }

                parse_vorbis_comment((const char*)comment_data, comment_len, metadata);
                comment_data += comment_len;
                remaining -= comment_len;
            }

            metadata->has_metadata = (metadata->title[0] != '\0' || metadata->artist[0] != '\0');
            return metadata->has_metadata;
        }

        offset += block_size;

        if (is_last) {
            break;
        }
    }

    return false;
}
