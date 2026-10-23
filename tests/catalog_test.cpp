#include "gui/image_store.h"
#include "gui/layout_engine.h"
#include <cassert>
#include <cmath>
#include <stdexcept>

struct CatalogTestAccess {
    static void check_visible_budget() {
        ImageStore store;
        store.MAX_DECODED_MEMORY = 64;
        store.MAX_SCALED_MEMORY = 64;
        std::vector<size_t> visible;
        for (int i = 0; i < 8; ++i) visible.push_back(store.add_image(std::to_string(i), 1));
        store.mark_visible(visible);
        for (auto id : visible) {
            store.set_thumbnail_rgb(id, store.get(id).filepath, ThumbQuality::SQUARE_64,
                std::vector<uint8_t>(48, 42), 4, 4, 1);
            assert(store.decoded_rgb_memory_used_ <= store.MAX_DECODED_MEMORY);
            assert(store.scaled_rgb_memory_used_ <= store.MAX_SCALED_MEMORY);
        }
        assert(!store.get(visible.back()).square_thumb.rgb_data.empty());
    }
};

int main() {
    CatalogTestAccess::check_visible_budget();
    ImageStore store;
    const auto a = store.add_image("a.jpg", 1, 10, 10);
    const auto b = store.add_image("b.jpg", 2, 20, 10);
    const auto c = store.add_image("c.jpg", 3, 30, 10);
    store.sort_entries(ImageStore::SortCriteria::ALPHABETICAL, false);
    assert(store.at_position(0).index == c);
    assert(store.get(a).filepath == "a.jpg");
    assert(store.get_filtered_aspects("").front().first == c);

    store.remove_image("a.jpg");
    assert(!store.contains(a));
    assert(store.get(b).filepath == "b.jpg");
    assert(store.get(c).filepath == "c.jpg");
    store.set_thumbnail_rgb(b, "b.jpg", ThumbQuality::SMALL,
                            std::vector<uint8_t>(6, 42), 2, 1, 1);
    assert(store.get(b).scaled.rgb_data.front() == 42);
    assert(store.get(c).scaled.rgb_data.empty());

    const auto replacement = store.add_image("a.jpg", 1, 10, 10);
    assert(replacement != a);
    store.set_thumbnail_rgb(a, "a.jpg", ThumbQuality::SMALL,
                            std::vector<uint8_t>(3, 99), 1, 1, 1);
    assert(store.get(replacement).scaled.rgb_data.empty());

    store.rename_image("b.jpg", "c.jpg");
    assert(!store.contains(c));
    assert(store.find_by_filepath("c.jpg") == b);
    assert(store.count() == 2);
    assert(store.get(b).index == b);
    bool rejected = false;
    try { store.get(a); } catch (const std::out_of_range&) { rejected = true; }
    assert(rejected);

    auto snapshot = store.snapshot();
    snapshot.get(b).similarity_score = 0.9;
    snapshot.get(replacement).similarity_score = 0.3;
    store.remove_image("a.jpg");
    const auto fresh = store.add_image("a.jpg", 1);
    assert(snapshot.contains(replacement));
    assert(!snapshot.contains(fresh));
    assert(store.apply_similarity(snapshot, b));
    assert(store.order().front() == b);
    assert(store.get(fresh).similarity_score == 0);
    ++store.get(b).revision;
    assert(!store.apply_similarity(snapshot, b));
    assert(snapshot.get(b).revision != store.get(b).revision);
    assert(snapshot.get(b).scaled.rgb_data.empty());

    // Public mutation APIs accept strings borrowed from the entry they mutate.
    const auto borrowed_path = store.get(b).filepath;
    store.remove_image(store.get(b).filepath);
    assert(store.find_by_filepath(borrowed_path) == static_cast<size_t>(-1));
    assert(!store.contains(b));
    assert(store.add_image(borrowed_path, 1) != b);

    store.clear();
    assert(store.add_image("b.jpg", 1) != b);

    LayoutEngine engine;
    auto jres = engine.compute_justified({{0, 1.5}}, 800.0, 150.0);
    assert(jres.boxes.size() == 1);
    assert(std::abs(jres.boxes[0].y - 5.0) < 1e-4);
    assert(std::abs(jres.total_height - (jres.boxes[0].y + jres.boxes[0].h + 5.0)) < 1e-4);
}
