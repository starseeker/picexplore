/*
 * pdfimg.hpp - Modern C++ header-only PDF writer for embedding image buffers as pages.
 *
 * Features strict PDF 1.4 compliance for compatibility with all PDF viewers.
 * Supports multiple compression methods with compile-time toggles.
 *
 * Compression Support:
 * - Flate (zlib/deflate) compression using integrated miniz (ENABLE_MINIZ_COMPRESSION)
 * - JPEG compression using integrated toojpeg (ENABLE_TOOJPEG_COMPRESSION)
 * - No compression (always available)
 *
 * Modern C++ features:
 * - Header-only design with all implementation inline
 * - RAII for automatic resource management
 * - std::vector and std::string for dynamic data
 * - Smart pointers for safe memory management
 * - Namespaces for organization
 * - Exception safety and const-correctness
 *
 * Derived from PDFGen (https://github.com/AndreRenaud/PDFGen)
 *
 * Copyright (c) 2017, Andre Renaud
 * Copyright (c) 2024, Modern C++ adaptation
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

#pragma once

#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <zlib.h>
#include "../utils.h"

namespace pdfimg {

// Image compression types
enum class CompressionType {
    None = 0,
    Flate,
    JPEG
};

// Exception class for PDF generation errors
class PDFException : public std::runtime_error {
public:
    explicit PDFException(const std::string& message) : std::runtime_error(message) {}
};

// Forward declarations
class PDFDocument;

// Image data container
class ImageData {
private:
    std::vector<uint8_t> data_;
    int width_;
    int height_;
    bool is_rgb_;
    CompressionType compression_;
    std::string filter_name_;

public:
    ImageData(const std::vector<uint8_t>& data, int width, int height, bool is_rgb,
              CompressionType compression = CompressionType::None,
              const std::string& filter_name = "")
        : data_(data), width_(width), height_(height), is_rgb_(is_rgb),
          compression_(compression), filter_name_(filter_name) {}

    // Move constructor for efficiency
    ImageData(std::vector<uint8_t>&& data, int width, int height, bool is_rgb,
              CompressionType compression = CompressionType::None,
              const std::string& filter_name = "")
        : data_(std::move(data)), width_(width), height_(height), is_rgb_(is_rgb),
          compression_(compression), filter_name_(filter_name) {}

    const std::vector<uint8_t>& data() const { return data_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool is_rgb() const { return is_rgb_; }
    CompressionType compression() const { return compression_; }
    const std::string& filter_name() const { return filter_name_; }

    size_t size() const { return data_.size(); }
};

// Image compressor interface
class ImageCompressor {
public:
    virtual ~ImageCompressor() = default;
    virtual std::unique_ptr<ImageData> compress(const uint8_t* raw_data, int width, int height,
                                                int stride, bool is_rgb) = 0;
};

// Flate (zlib) compressor using zlib
class FlateCompressor : public ImageCompressor {
public:
    std::unique_ptr<ImageData> compress(const uint8_t* raw_data, int width, int height,
                                        int stride, bool is_rgb) override {
        const int bytes_per_pixel = is_rgb ? 3 : 1;
        const size_t raw_size = static_cast<size_t>(width) * height * bytes_per_pixel;
        const uint8_t* src_ptr = raw_data;
        std::vector<uint8_t> uncompressed_data;

        if (stride != width * bytes_per_pixel) {
            uncompressed_data.reserve(raw_size);
            for (int y = 0; y < height; ++y) {
                const uint8_t* row_start = raw_data + static_cast<size_t>(y) * stride;
                uncompressed_data.insert(uncompressed_data.end(), row_start,
                                         row_start + width * bytes_per_pixel);
            }
            src_ptr = uncompressed_data.data();
        }

        uLongf compressed_size = compressBound(static_cast<uLong>(raw_size));
        std::vector<uint8_t> compressed_data(compressed_size);

        int result = compress2(compressed_data.data(), &compressed_size,
                               src_ptr, static_cast<uLong>(raw_size), 6);

        if (result != Z_OK) {
            throw PDFException("Flate compression failed");
        }

        compressed_data.resize(compressed_size);

        return std::make_unique<ImageData>(std::move(compressed_data), width, height,
                                           is_rgb, CompressionType::Flate, " /Filter /FlateDecode");
    }
};

// JPEG compressor using shared libjpeg encoder
class JPEGCompressor : public ImageCompressor {
public:
    std::unique_ptr<ImageData> compress(const uint8_t* raw_data, int width, int height,
                                        int stride, bool is_rgb) override {
        const int bytes_per_pixel = is_rgb ? 3 : 1;
        std::vector<uint8_t> rgb_data;
        const uint8_t* rgb_ptr = raw_data;

        if (!is_rgb || stride != width * 3) {
            rgb_data.resize(static_cast<size_t>(width) * height * 3);
            for (int y = 0; y < height; ++y) {
                const uint8_t* row_start = raw_data + static_cast<size_t>(y) * stride;
                uint8_t* dst_row = rgb_data.data() + static_cast<size_t>(y) * width * 3;
                if (is_rgb) {
                    std::copy(row_start, row_start + width * 3, dst_row);
                } else {
                    for (int x = 0; x < width; ++x) {
                        dst_row[x * 3 + 0] = row_start[x];
                        dst_row[x * 3 + 1] = row_start[x];
                        dst_row[x * 3 + 2] = row_start[x];
                    }
                }
            }
            rgb_ptr = rgb_data.data();
        }

        auto jpeg_bytes = encode_jpeg(rgb_ptr, width, height, 85);
        if (jpeg_bytes.empty()) {
            throw PDFException("JPEG compression failed");
        }

        return std::make_unique<ImageData>(std::move(jpeg_bytes), width, height,
                                           true, CompressionType::JPEG, " /Filter /DCTDecode");
    }
};

// No compression compressor (identity)
class NoCompressor : public ImageCompressor {
public:
    std::unique_ptr<ImageData> compress(const uint8_t* raw_data, int width, int height,
                                        int stride, bool is_rgb) override {
        const int bytes_per_pixel = is_rgb ? 3 : 1;
        std::vector<uint8_t> image_data;
        image_data.reserve(width * height * bytes_per_pixel);

        // Copy data row by row, handling stride
        for (int y = 0; y < height; ++y) {
            const uint8_t* row_start = raw_data + y * stride;
            image_data.insert(image_data.end(), row_start,
                            row_start + width * bytes_per_pixel);
        }

        return std::make_unique<ImageData>(std::move(image_data), width, height,
                                          is_rgb, CompressionType::None, "");
    }
};

// PDF Document class
class PDFDocument {
private:
    std::vector<uint8_t> data_;
    std::vector<size_t> object_offsets_;
    std::vector<int> page_objects_;
    size_t xref_offset_;
    int next_object_id_;

    void reserve_capacity(size_t additional_size) {
        if (data_.capacity() < data_.size() + additional_size) {
            data_.reserve(std::max(data_.capacity() * 2, data_.size() + additional_size));
        }
    }

    void append_string(const std::string& str) {
        reserve_capacity(str.size());
        data_.insert(data_.end(), str.begin(), str.end());
    }

    void append_formatted(const char* format, ...) {
        va_list args;
        va_start(args, format);

        // Calculate required size
        va_list args_copy;
        va_copy(args_copy, args);
        int size = vsnprintf(nullptr, 0, format, args_copy);
        va_end(args_copy);

        if (size < 0) {
            va_end(args);
            throw PDFException("Format string error");
        }

        // Format the string
        std::vector<char> buffer(size + 1);
        vsnprintf(buffer.data(), buffer.size(), format, args);
        va_end(args);

        // Append to data
        append_string(std::string(buffer.data(), size));
    }

    void add_object_offset() {
        object_offsets_.push_back(data_.size());
    }

public:
    PDFDocument() : xref_offset_(0), next_object_id_(1) {
        data_.reserve(4096);
        object_offsets_.reserve(32);
        page_objects_.reserve(16);

        // PDF header
        const std::string header = "%PDF-1.4\n";
        append_string(header);
    }

    // Disable copy constructor and assignment (move-only class)
    PDFDocument(const PDFDocument&) = delete;
    PDFDocument& operator=(const PDFDocument&) = delete;

    // Enable move constructor and assignment
    PDFDocument(PDFDocument&&) = default;
    PDFDocument& operator=(PDFDocument&&) = default;

    // Add an image page using pre-compressed image data
    void add_image_page(const ImageData& image_data, double dpi = 72.0) {
        // PDF units: 1/72 inch
        const double page_width = image_data.width() * 72.0 / dpi;
        const double page_height = image_data.height() * 72.0 / dpi;

        // Assign object IDs
        const int img_obj = next_object_id_++;
        const int content_obj = next_object_id_++;
        const int page_obj = next_object_id_++;

        // Add image object
        add_object_offset();
        append_formatted("%d 0 obj\n", img_obj);
        append_formatted("<< /Type /XObject /Subtype /Image /Width %d /Height %d "
                        "/ColorSpace /Device%s /BitsPerComponent 8%s /Length %zu >>\nstream\n",
                        image_data.width(), image_data.height(),
                        image_data.is_rgb() ? "RGB" : "Gray",
                        image_data.filter_name().c_str(), image_data.size());

        // Add image data
        reserve_capacity(image_data.size());
        data_.insert(data_.end(), image_data.data().begin(), image_data.data().end());
        append_string("\nendstream\nendobj\n");

        // Add content stream
        std::ostringstream content_stream;
        content_stream << "q\n" << page_width << " 0 0 " << page_height << " 0 0 cm\n/Im0 Do\nQ\n";
        const std::string content_str = content_stream.str();

        add_object_offset();
        append_formatted("%d 0 obj\n<< /Length %zu >>\nstream\n", content_obj, content_str.size());
        append_string(content_str);
        append_string("endstream\nendobj\n");

        // Add page object
        add_object_offset();
        append_formatted("%d 0 obj\n"
                        "<< /Type /Page /MediaBox [0 0 %.2f %.2f] "
                        "/Contents %d 0 R /Resources << /XObject <</Im0 %d 0 R>> >> >>\nendobj\n",
                        page_obj, page_width, page_height, content_obj, img_obj);

        page_objects_.push_back(page_obj);
    }

    // Add an image page from raw image data with specified compression
    void add_image_page(const uint8_t* raw_data, int width, int height, int stride,
                       bool is_rgb, CompressionType compression = CompressionType::None,
                       double dpi = 72.0) {
        std::unique_ptr<ImageCompressor> compressor;

        switch (compression) {
            case CompressionType::Flate:
                compressor = std::make_unique<FlateCompressor>();
                break;
            case CompressionType::JPEG:
                compressor = std::make_unique<JPEGCompressor>();
                break;
            case CompressionType::None:
            default:
                compressor = std::make_unique<NoCompressor>();
                break;
        }

        auto compressed_image = compressor->compress(raw_data, width, height, stride, is_rgb);
        add_image_page(*compressed_image, dpi);
    }

    // Save PDF to file
    bool save(const std::string& filename) {
        try {
            // Complete the PDF structure
            finalize_pdf();

            // Write to file
            std::ofstream file(filename, std::ios::binary);
            if (!file) {
                return false;
            }

            file.write(reinterpret_cast<const char*>(data_.data()), data_.size());
            return file.good();
        } catch (const std::exception&) {
            return false;
        }
    }

private:
    void finalize_pdf() {
        if (page_objects_.empty()) {
            throw PDFException("No pages added to PDF");
        }

        // Assign object IDs for Pages and Catalog
        const int pages_obj = next_object_id_++;
        const int catalog_obj = next_object_id_++;

        // Add Pages object
        add_object_offset();
        append_formatted("%d 0 obj\n<< /Type /Pages /Kids [", pages_obj);
        for (int page_obj : page_objects_) {
            append_formatted("%d 0 R ", page_obj);
        }
        append_formatted("] /Count %zu >>\nendobj\n", page_objects_.size());

        // Add Catalog object
        add_object_offset();
        append_formatted("%d 0 obj\n<< /Type /Catalog /Pages %d 0 R >>\nendobj\n",
                        catalog_obj, pages_obj);

        // Add xref table
        xref_offset_ = data_.size();
        append_formatted("xref\n0 %zu\n0000000000 65535 f \n", object_offsets_.size() + 1);
        for (size_t offset : object_offsets_) {
            append_formatted("%010zu 00000 n \n", offset);
        }

        // Add trailer
        append_formatted("trailer\n<< /Size %zu /Root %d 0 R >>\nstartxref\n%zu\n%%EOF\n",
                        object_offsets_.size() + 1, catalog_obj, xref_offset_);
    }
};

// Factory function to create compressor based on type
inline std::unique_ptr<ImageCompressor> create_compressor(CompressionType type) {
    switch (type) {
        case CompressionType::Flate:
            return std::make_unique<FlateCompressor>();
        case CompressionType::JPEG:
            return std::make_unique<JPEGCompressor>();
        case CompressionType::None:
        default:
            return std::make_unique<NoCompressor>();
    }
}

// Utility function to get available compression methods
inline std::vector<CompressionType> get_available_compression_methods() {
    return {CompressionType::None, CompressionType::Flate, CompressionType::JPEG};
}

// Utility function to convert compression type to string
inline std::string compression_type_to_string(CompressionType type) {
    switch (type) {
        case CompressionType::None: return "none";
        case CompressionType::Flate: return "flate";
        case CompressionType::JPEG: return "jpeg";
        default: return "unknown";
    }
}

// Utility function to parse compression type from string
inline CompressionType compression_type_from_string(const std::string& str) {
    if (str == "none") return CompressionType::None;
    if (str == "flate") return CompressionType::Flate;
    if (str == "jpeg") return CompressionType::JPEG;
    throw PDFException("Unknown compression type: " + str);
}

} // namespace pdfimg
