#include <iostream>
#include <fstream>
#include <cassert>
#include <filesystem>
#include "database.h"

namespace fs = std::filesystem;

int main() {
    std::string test_db = "/tmp/test_picexplore_dup.db";
    if (fs::exists(test_db)) {
        fs::remove(test_db);
    }

    DatabaseManager db;
    bool ok = db.open(test_db);
    assert(ok && "Failed to open test database");

    std::string test_hash = "abcdef0123456789abcdef0123456789";
    std::string p1 = "/photos/summer/photo1.jpg";
    std::string p2 = "/photos/backup/photo1.jpg";
    std::string p3 = "/photos/archive/photo1.jpg";

    // 1. Initial state: no paths
    auto paths = db.get_paths_for_hash(test_hash);
    assert(paths.empty());

    // 2. Add first path
    ok = db.add_path_for_hash(test_hash, p1);
    assert(ok);
    paths = db.get_paths_for_hash(test_hash);
    assert(paths.size() == 1);
    assert(paths[0] == p1);

    // 3. Add duplicate path
    ok = db.add_path_for_hash(test_hash, p2);
    assert(ok);
    paths = db.get_paths_for_hash(test_hash);
    assert(paths.size() == 2);
    assert(paths[0] == p1);
    assert(paths[1] == p2);

    // 4. Add third path
    ok = db.add_path_for_hash(test_hash, p3);
    assert(ok);
    paths = db.get_paths_for_hash(test_hash);
    assert(paths.size() == 3);

    // 5. Adding duplicate path already in set should not create duplicate entries
    ok = db.add_path_for_hash(test_hash, p2);
    assert(ok);
    paths = db.get_paths_for_hash(test_hash);
    assert(paths.size() == 3);

    // 6. Remove middle path
    ok = db.remove_path_for_hash(test_hash, p2);
    assert(ok);
    paths = db.get_paths_for_hash(test_hash);
    assert(paths.size() == 2);
    assert(paths[0] == p1);
    assert(paths[1] == p3);

    // 7. Test legacy :path backward compatibility
    std::string legacy_hash = "11112222333344445555666677778888";
    std::string legacy_path = "/photos/legacy.jpg";
    if (db.begin_transaction()) {
        db.store_key_value(legacy_hash + ":path", legacy_path);
        db.commit_transaction();
    }
    auto legacy_paths = db.get_paths_for_hash(legacy_hash);
    assert(legacy_paths.size() == 1);
    assert(legacy_paths[0] == legacy_path);

    // Adding to legacy hash should migrate to :paths
    std::string new_dup = "/photos/copy_of_legacy.jpg";
    ok = db.add_path_for_hash(legacy_hash, new_dup);
    assert(ok);
    legacy_paths = db.get_paths_for_hash(legacy_hash);
    assert(legacy_paths.size() == 2);
    assert(legacy_paths[0] == legacy_path);
    assert(legacy_paths[1] == new_dup);

    // 8. Test ImageMetadata storage and retrieval directly
    ImageMetadata test_meta{1234567, 1700000000, 1920, 1080};
    if (db.begin_transaction()) {
        db.store_image_metadata(test_hash, test_meta);
        db.commit_transaction();
    }
    ImageMetadata fetched_meta;
    if (db.begin_transaction()) {
        ok = db.get_image_metadata(test_hash, fetched_meta);
        db.commit_transaction();
        assert(ok);
        assert(fetched_meta.file_size == 1234567);
        assert(fetched_meta.file_timestamp == 1700000000);
        assert(fetched_meta.orig_width == 1920);
        assert(fetched_meta.orig_height == 1080);
    }

    // 9. Test directory scanning with multiple duplicate files and metadata caching
    std::string test_dir = "/tmp/test_dup_scan_dir";
    fs::create_directories(test_dir + "/sub1");
    fs::create_directories(test_dir + "/sub2");
    
    // Create 3 identical valid JPEG files using encode_jpeg
    std::vector<uint8_t> rgb_64(64 * 64 * 3, 128);
    std::vector<uint8_t> jpg_data = encode_jpeg(rgb_64.data(), 64, 64, 90);
    assert(!jpg_data.empty());
    {
        std::ofstream f1(test_dir + "/sub1/a.jpg", std::ios::binary);
        f1.write(reinterpret_cast<const char*>(jpg_data.data()), jpg_data.size());
        std::ofstream f2(test_dir + "/sub2/a_copy.jpg", std::ios::binary);
        f2.write(reinterpret_cast<const char*>(jpg_data.data()), jpg_data.size());
        std::ofstream f3(test_dir + "/a_dup.jpg", std::ios::binary);
        f3.write(reinterpret_cast<const char*>(jpg_data.data()), jpg_data.size());
    }

    std::string scan_db_path = "/tmp/test_dup_scan.db";
    if (fs::exists(scan_db_path)) fs::remove(scan_db_path);

    DatabaseManager scan_db;
    assert(scan_db.open(scan_db_path));
    Timer timer;
    StatusReporter reporter(10);
    int scanned = scan_db.scan_directory_parallel(test_dir, timer, reporter, 2);
    assert(scanned == 3);

    auto all_imgs = scan_db.get_all_images();
    assert(all_imgs.size() == 3);
    assert(!all_imgs[0].hash.empty());
    assert(all_imgs[0].hash == all_imgs[1].hash);
    assert(all_imgs[1].hash == all_imgs[2].hash);

    // Verify metadata was cached and loaded
    assert(all_imgs[0].file_size == jpg_data.size());
    assert(all_imgs[0].file_timestamp > 0);
    assert(all_imgs[0].orig_width == 64);
    assert(all_imgs[0].orig_height == 64);

    // Verify square thumbnail was generated and stored in DB
    std::vector<uint8_t> sq128_check, sq64_check;
    if (scan_db.begin_transaction()) {
        assert(scan_db.get_key_data(all_imgs[0].hash + ":sq128", sq128_check));
        assert(!sq128_check.empty());
        assert(scan_db.get_key_data(all_imgs[0].hash + ":sq64", sq64_check));
        assert(!sq64_check.empty());
        scan_db.abort_transaction();
    }

    auto dup_paths = scan_db.get_paths_for_hash(all_imgs[0].hash);
    assert(dup_paths.size() == 3);

    scan_db.close();
    fs::remove_all(test_dir);
    fs::remove(scan_db_path);

    db.close();
    fs::remove(test_db);

    std::cout << "All database duplicate and metadata tests passed successfully!" << std::endl;
    return 0;
}
