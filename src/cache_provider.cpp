/*
 * cache_provider.cpp - Unified LRU cache provider implementation for picexplore
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "cache_provider.hpp"
#include <vector>

//==============================================================================
// Template Specializations for Size Calculation
//==============================================================================

/**
 * Specialized size calculation for vector<uint8_t> (thumbnail data)
 */
template<>
size_t CacheProvider<std::vector<uint8_t>>::calculate_size(
    const std::vector<uint8_t>& data, size_t provided_size) const {
    if (provided_size > 0) {
        return provided_size;
    }
    // For vector<uint8_t>, return the actual data size plus overhead
    return data.size() + sizeof(std::vector<uint8_t>);
}

//==============================================================================
// Explicit Template Instantiations
//==============================================================================

// Instantiate for thumbnail data (vector<uint8_t>)
template class CacheProvider<std::vector<uint8_t>>;

// Instantiate for string data (if needed for other caches)
template class CacheProvider<std::string>;

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s