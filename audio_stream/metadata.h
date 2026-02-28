///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Audio Stream Metadata Extraction
//
// Parses metadata from various audio formats:
// - ID3v1/ID3v2 for MP3
// - Vorbis comments for OGG Vorbis and FLAC
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Metadata structure to hold extracted tags

#define METADATA_MAX_STRING 256

typedef struct AudioMetadata {
    char title[METADATA_MAX_STRING];
    char artist[METADATA_MAX_STRING];
    char album[METADATA_MAX_STRING];
    char date[METADATA_MAX_STRING];
    char genre[METADATA_MAX_STRING];
    bool has_metadata;
} AudioMetadata;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Metadata extraction functions

// Extract ID3 tags from MP3 data (checks both ID3v2 at start and ID3v1 at end)
bool metadata_extract_id3(const uint8_t* data, uint64_t size, AudioMetadata* metadata);

// Extract Vorbis comments from OGG Vorbis data
bool metadata_extract_vorbis_comments(const uint8_t* data, uint64_t size, AudioMetadata* metadata);

// Extract Vorbis comments from FLAC data
bool metadata_extract_flac_comments(const uint8_t* data, uint64_t size, AudioMetadata* metadata);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif
