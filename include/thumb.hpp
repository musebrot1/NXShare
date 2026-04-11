#pragma once
#include <string>
#include <vector>
#include <stdint.h>

// Extract embedded cover image from an MP4 file
// Returns JPEG/PNG bytes if found, empty vector otherwise
std::vector<uint8_t> extractVideoThumbnail(const std::string& filepath);
