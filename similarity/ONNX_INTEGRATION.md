/*
 * onnx_integration_notes.md - Plans for ONNX Runtime integration
 *
 * This document outlines the integration path for replacing the current
 * simple feature extraction with ONNX Runtime and MobileNetV2/V3 models.
 */

# ONNX Runtime Integration Plan

## Current Status

The similarity search system currently uses a simple but effective 64-dimensional feature extraction:
- Color histograms (48 features)
- Texture analysis (8 features) 
- Spatial features (8 features)

This provides good results for basic similarity but can be significantly improved with deep learning features.

## Integration Steps

### 1. ONNX Runtime Setup

```cpp
// Add to ImageSimilarityIndex constructor
bool ImageSimilarityIndex::initialize_onnx() {
    // Initialize ONNX Runtime environment
    OrtEnv* env = nullptr;
    OrtStatus* status = OrtCreateEnv(ORT_LOGGING_LEVEL_WARNING, "ImageSimilarity", &env);
    if (status != nullptr) {
        return false;
    }
    ort_env_.reset(env);
    
    // Create session options
    OrtSessionOptions* session_options = nullptr;
    status = OrtCreateSessionOptions(&session_options);
    if (status != nullptr) {
        return false;
    }
    ort_session_options_.reset(session_options);
    
    // Load the model
    if (config_.model_path.empty()) {
        std::cerr << "Warning: No ONNX model specified, using simple features" << std::endl;
        return false;
    }
    
    OrtSession* session = nullptr;
    status = OrtCreateSession(env, config_.model_path.c_str(), session_options, &session);
    if (status != nullptr) {
        std::cerr << "Error: Failed to load ONNX model: " << config_.model_path << std::endl;
        return false;
    }
    ort_session_.reset(session);
    
    return true;
}
```

### 2. Model Preprocessing

```cpp
bool ImageSimilarityIndex::preprocess_image(const uint8_t* image_data, int width, int height,
                                           std::vector<float>& output_data, 
                                           int& output_width, int& output_height) {
    // MobileNetV2/V3 expects 224x224 RGB input
    const int target_size = 224;
    output_width = target_size;
    output_height = target_size;
    
    // Resize image to 224x224 using stb_image_resize
    std::vector<uint8_t> resized(target_size * target_size * 3);
    if (!stbir_resize_uint8_linear(image_data, width, height, 0,
                                  resized.data(), target_size, target_size, 0, 
                                  STBIR_RGB)) {
        return false;
    }
    
    // Convert to float and normalize to [0, 1] (or model-specific normalization)
    output_data.resize(target_size * target_size * 3);
    for (int i = 0; i < target_size * target_size * 3; i++) {
        // Standard ImageNet normalization for MobileNet
        float normalized = resized[i] / 255.0f;
        
        // Apply ImageNet mean/std normalization if needed:
        // mean = [0.485, 0.456, 0.406], std = [0.229, 0.224, 0.225]
        int channel = i % 3;
        const float means[] = {0.485f, 0.456f, 0.406f};
        const float stds[] = {0.229f, 0.224f, 0.225f};
        
        output_data[i] = (normalized - means[channel]) / stds[channel];
    }
    
    return true;
}
```

### 3. ONNX Inference

```cpp
bool ImageSimilarityIndex::extract_features_onnx(const uint8_t* image_data, int width, int height,
                                                 std::vector<float>& feature_vector) {
    if (!ort_session_) {
        // Fallback to simple features if ONNX not initialized
        return extract_features(image_data, width, height, feature_vector);
    }
    
    // Preprocess image
    std::vector<float> input_data;
    int input_width, input_height;
    if (!preprocess_image(image_data, width, height, input_data, input_width, input_height)) {
        return false;
    }
    
    // Create input tensor
    const int64_t input_shape[] = {1, 3, input_height, input_width}; // NCHW format
    OrtValue* input_tensor = nullptr;
    OrtStatus* status = OrtCreateTensorWithDataAsOrtValue(
        ort_memory_info_.get(),
        input_data.data(),
        input_data.size() * sizeof(float),
        input_shape,
        4,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input_tensor
    );
    
    if (status != nullptr) {
        return false;
    }
    
    // Run inference
    const char* input_names[] = {"input"};  // Adjust based on model
    const char* output_names[] = {"output"}; // Adjust based on model
    OrtValue* output_tensor = nullptr;
    
    status = OrtRun(
        ort_session_.get(),
        nullptr,  // run options
        input_names,
        &input_tensor,
        1,  // num inputs
        output_names,
        1,  // num outputs
        &output_tensor
    );
    
    if (status != nullptr) {
        OrtReleaseValue(input_tensor);
        return false;
    }
    
    // Extract feature vector from output tensor
    float* output_data = nullptr;
    status = OrtGetTensorMutableData(output_tensor, (void**)&output_data);
    if (status != nullptr) {
        OrtReleaseValue(input_tensor);
        OrtReleaseValue(output_tensor);
        return false;
    }
    
    // Get output tensor info to determine feature vector size
    OrtTensorTypeAndShapeInfo* output_info = nullptr;
    status = OrtGetTensorTypeAndShape(output_tensor, &output_info);
    if (status != nullptr) {
        OrtReleaseValue(input_tensor);
        OrtReleaseValue(output_tensor);
        return false;
    }
    
    size_t num_elements = 0;
    status = OrtGetTensorShapeElementCount(output_info, &num_elements);
    
    // Copy output to feature vector
    feature_vector.assign(output_data, output_data + num_elements);
    
    // Apply L2 normalization for cosine similarity
    double norm = 0.0;
    for (float val : feature_vector) {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    
    if (norm > 0.0) {
        for (float& val : feature_vector) {
            val /= norm;
        }
    }
    
    // Cleanup
    OrtReleaseTensorTypeAndShapeInfo(output_info);
    OrtReleaseValue(input_tensor);
    OrtReleaseValue(output_tensor);
    
    return true;
}
```

### 4. Model Download and Setup

```bash
# Download a pre-trained MobileNetV2 ONNX model
wget https://github.com/onnx/models/raw/main/vision/classification/mobilenet/model/mobilenetv2-7.onnx

# Or MobileNetV3
wget https://github.com/onnx/models/raw/main/vision/classification/mobilenet/model/mobilenetv3-large-1.0.onnx

# Use with simfind
simfind --query photo.jpg --database images.db --model mobilenetv2-7.onnx
```

### 5. CMakeLists.txt Updates

```cmake
# Find ONNX Runtime
find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h 
          PATHS /usr/local/include/onnxruntime)
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime
             PATHS /usr/local/lib)

if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    set(ONNXRUNTIME_FOUND TRUE)
    target_include_directories(similarity_index PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
    target_link_libraries(similarity_index ${ONNXRUNTIME_LIBRARY})
    target_compile_definitions(similarity_index PRIVATE HAVE_ONNXRUNTIME)
else()
    message(STATUS "ONNX Runtime not found, using simple feature extraction")
endif()
```

### 6. Configuration Updates

```cpp
struct SimilarityConfig {
    std::string model_path;           // Path to ONNX model file
    std::string cache_db_path;        // Optional LMDB cache path for feature vectors
    bool use_cache = true;            // Whether to use LMDB caching
    bool use_lsh = true;              // Use LSH instead of KNN for large datasets
    int feature_vector_size = 1280;   // MobileNetV2/V3 feature size
    bool use_onnx = true;             // Prefer ONNX if available
    
    // Model-specific settings
    struct ModelSettings {
        int input_size = 224;         // Input image size
        bool apply_imagenet_norm = true; // Apply ImageNet normalization
        std::vector<float> mean = {0.485f, 0.456f, 0.406f};
        std::vector<float> std = {0.229f, 0.224f, 0.225f};
    } model_settings;
};
```

## Expected Performance Improvements

### Feature Quality
- **Current**: 64-dimensional hand-crafted features
- **With ONNX**: 1280-dimensional learned features from MobileNetV2/V3
- **Expected**: Significantly better semantic similarity detection

### Performance Impact
- **Feature Extraction**: Slower (~50-200ms per image vs <1ms)  
- **Index Size**: ~20x larger (1280 vs 64 dimensions)
- **Query Time**: Similar (mlpack handles high-dimensional data well)
- **Memory Usage**: ~20x more RAM for feature vectors

### Quality vs Speed Trade-offs
- **Simple Features**: Fast, good for basic similarity
- **ONNX Features**: Slower, excellent for semantic similarity
- **Hybrid Approach**: Use simple features for rapid prototyping, ONNX for production

## Installation Instructions

```bash
# Install ONNX Runtime (example for Ubuntu)
wget https://github.com/microsoft/onnxruntime/releases/download/v1.16.0/onnxruntime-linux-x64-1.16.0.tgz
tar -xzf onnxruntime-linux-x64-1.16.0.tgz
sudo cp -r onnxruntime-linux-x64-1.16.0/include/* /usr/local/include/
sudo cp -r onnxruntime-linux-x64-1.16.0/lib/* /usr/local/lib/
sudo ldconfig

# Rebuild with ONNX Runtime support
cd build && cmake .. && make -j$(nproc)
```

## Testing ONNX Integration

```bash
# Test with ONNX model
simfind --query test.jpg --database images.db --model mobilenetv2-7.onnx --verbose

# Compare with simple features (no model specified)
simfind --query test.jpg --database images.db --verbose

# Performance benchmark
time simfind --query test.jpg --database large_images.db --model mobilenetv2-7.onnx
```

This integration path provides a clear upgrade path from the current working system to a state-of-the-art deep learning based similarity search.