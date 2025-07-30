/*
 * test_thumbnail_key.cpp - Test for thumbnail key utility functions
 *
 * Copyright (c) 2025 Clifford Yapp
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "../src/utils.hpp"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "Testing thumbnail key utilities..." << std::endl;

    // Test make_thumbnail_key with single size
    std::string hash = "abc123def456";
    std::string key1 = make_thumbnail_key(hash, 256);
    std::string expected1 = "abc123def456:256";
    assert(key1 == expected1);
    std::cout << "✓ make_thumbnail_key(hash, size) test passed: " << key1 << std::endl;

    // Test make_thumbnail_key with width/height (width > height)
    std::string key2 = make_thumbnail_key(hash, 256, 128);
    std::string expected2 = "abc123def456:256";  // max(256, 128) = 256
    assert(key2 == expected2);
    std::cout << "✓ make_thumbnail_key(hash, width, height) width>height test passed: " << key2 << std::endl;

    // Test make_thumbnail_key with width/height (height > width)
    std::string key3 = make_thumbnail_key(hash, 128, 256);
    std::string expected3 = "abc123def456:256";  // max(128, 256) = 256
    assert(key3 == expected3);
    std::cout << "✓ make_thumbnail_key(hash, width, height) height>width test passed: " << key3 << std::endl;

    // Test make_thumbnail_key with width/height (equal dimensions)
    std::string key4 = make_thumbnail_key(hash, 256, 256);
    std::string expected4 = "abc123def456:256";  // max(256, 256) = 256
    assert(key4 == expected4);
    std::cout << "✓ make_thumbnail_key(hash, width, height) equal dimensions test passed: " << key4 << std::endl;

    // Test edge case with zero dimensions
    std::string key5 = make_thumbnail_key(hash, 0, 100);
    std::string expected5 = "abc123def456:100";  // max(0, 100) = 100
    assert(key5 == expected5);
    std::cout << "✓ make_thumbnail_key(hash, width, height) zero width test passed: " << key5 << std::endl;

    // Test different hash
    std::string hash2 = "fedcba987654";
    std::string key6 = make_thumbnail_key(hash2, 512);
    std::string expected6 = "fedcba987654:512";
    assert(key6 == expected6);
    std::cout << "✓ make_thumbnail_key different hash test passed: " << key6 << std::endl;

    // Test format consistency - all these should produce the same key
    std::string consistency_hash = "test123";
    std::string key_from_size = make_thumbnail_key(consistency_hash, 200);
    std::string key_from_w_h_1 = make_thumbnail_key(consistency_hash, 200, 150);
    std::string key_from_w_h_2 = make_thumbnail_key(consistency_hash, 150, 200);
    std::string key_from_w_h_3 = make_thumbnail_key(consistency_hash, 200, 200);
    
    assert(key_from_size == key_from_w_h_1);
    assert(key_from_size == key_from_w_h_2);
    assert(key_from_size == key_from_w_h_3);
    std::cout << "✓ Format consistency test passed: " << key_from_size << std::endl;

    std::cout << "\nAll thumbnail key utility tests passed! ✅" << std::endl;
    return 0;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s