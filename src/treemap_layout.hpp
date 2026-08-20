#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <map>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

struct TreemapItem {
    size_t id = 0;
    double weight = 1.0;
    double aspect_ratio = 1.0;
};

struct TreemapBox {
    size_t id = 0;
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

struct HierarchicalTreemapItem {
    size_t id = 0;
    std::string filepath;
    double weight = 1.0;
    double aspect_ratio = 1.0;
};

struct ContainerBox {
    std::string dir_path;
    std::string dir_name;
    int depth = 0;
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

struct HierarchicalTreemapResult {
    std::vector<TreemapBox> boxes;
    std::vector<ContainerBox> container_boxes;
};

class SquarifiedTreemap {
public:
    static std::vector<TreemapBox> compute(const std::vector<TreemapItem>& input_items,
                                          double bounds_x, double bounds_y,
                                          double bounds_w, double bounds_h,
                                          double padding = 2.0) {
        std::vector<TreemapBox> results;
        if (input_items.empty() || bounds_w <= 0.0 || bounds_h <= 0.0) {
            return results;
        }

        // Copy and sort items descending by weight
        std::vector<TreemapItem> items = input_items;
        std::sort(items.begin(), items.end(), [](const TreemapItem& a, const TreemapItem& b) {
            return a.weight > b.weight;
        });

        // Compute total weight (ensuring positive weights)
        double total_weight = 0.0;
        for (const auto& item : items) {
            total_weight += std::max(1e-9, item.weight);
        }

        if (total_weight <= 0.0) {
            return results;
        }

        // Calculate normalized areas for each item matching total bounding area
        double total_area = bounds_w * bounds_h;
        std::vector<double> areas(items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            areas[i] = total_area * (std::max(1e-9, items[i].weight) / total_weight);
        }

        results.reserve(items.size());

        double cur_x = bounds_x;
        double cur_y = bounds_y;
        double cur_w = bounds_w;
        double cur_h = bounds_h;

        // O(1) worst ratio evaluator for descending sorted areas
        auto worst_ratio = [&](size_t start_idx, size_t end_idx, double row_sum, double side_length) -> double {
            if (side_length <= 0.0 || row_sum <= 0.0) {
                return std::numeric_limits<double>::max();
            }
            double a_max = areas[start_idx];
            double a_min = areas[end_idx];
            if (a_min <= 0.0) return std::numeric_limits<double>::max();

            double s2 = side_length * side_length;
            double sum2 = row_sum * row_sum;
            double r1 = (s2 * a_max) / sum2;
            double r2 = sum2 / (s2 * a_min);
            return std::max(r1, r2);
        };

        auto layout_row = [&](size_t start_idx, size_t end_idx, double row_sum) {
            if (cur_w <= 0.0 || cur_h <= 0.0) return;

            bool vertical_strip = (cur_w >= cur_h);
            double side_length = vertical_strip ? cur_h : cur_w;
            double strip_thickness = (side_length > 0.0) ? (row_sum / side_length) : 0.0;

            if (vertical_strip) {
                double strip_w = std::min(strip_thickness, cur_w);
                double item_y = cur_y;
                for (size_t idx = start_idx; idx <= end_idx; ++idx) {
                    double item_h = (strip_w > 0.0) ? (areas[idx] / strip_w) : 0.0;
                    if (idx == end_idx) {
                        // Avoid rounding gaps at the strip end
                        item_h = std::max(0.0, (cur_y + cur_h) - item_y);
                    }

                    results.push_back(TreemapBox{
                        items[idx].id,
                        cur_x,
                        item_y,
                        strip_w,
                        item_h
                    });
                    item_y += item_h;
                }
                cur_x += strip_w;
                cur_w = std::max(0.0, cur_w - strip_w);
            } else {
                double strip_h = std::min(strip_thickness, cur_h);
                double item_x = cur_x;
                for (size_t idx = start_idx; idx <= end_idx; ++idx) {
                    double item_w = (strip_h > 0.0) ? (areas[idx] / strip_h) : 0.0;
                    if (idx == end_idx) {
                        item_w = std::max(0.0, (cur_x + cur_w) - item_x);
                    }

                    results.push_back(TreemapBox{
                        items[idx].id,
                        item_x,
                        cur_y,
                        item_w,
                        strip_h
                    });
                    item_x += item_w;
                }
                cur_y += strip_h;
                cur_h = std::max(0.0, cur_h - strip_h);
            }
        };

        size_t row_start = 0;
        size_t row_end = 0;
        double row_area_sum = 0.0;
        bool in_row = false;

        for (size_t i = 0; i < items.size(); ++i) {
            double side = std::min(cur_w, cur_h);
            if (side <= 0.0) {
                if (in_row) {
                    layout_row(row_start, row_end, row_area_sum);
                    in_row = false;
                }
                results.push_back(TreemapBox{items[i].id, cur_x, cur_y, 0.0, 0.0});
                continue;
            }

            if (!in_row) {
                row_start = i;
                row_end = i;
                row_area_sum = areas[i];
                in_row = true;
            } else {
                double current_worst = worst_ratio(row_start, row_end, row_area_sum, side);
                double candidate_sum = row_area_sum + areas[i];
                double candidate_worst = worst_ratio(row_start, i, candidate_sum, side);

                if (candidate_worst <= current_worst) {
                    row_end = i;
                    row_area_sum = candidate_sum;
                } else {
                    layout_row(row_start, row_end, row_area_sum);
                    row_start = i;
                    row_end = i;
                    row_area_sum = areas[i];
                }
            }
        }

        if (in_row) {
            layout_row(row_start, row_end, row_area_sum);
        }

        // Apply padding between rectangles if requested
        if (padding > 0.0) {
            double half_pad = padding / 2.0;
            for (auto& box : results) {
                if (box.w > padding && box.h > padding) {
                    box.x += half_pad;
                    box.y += half_pad;
                    box.w -= padding;
                    box.h -= padding;
                }
            }
        }

        return results;
    }
};

class HierarchicalTreemap {
public:
    struct TreeNode {
        std::string name;
        std::string full_path;
        int depth = 0;
        double total_weight = 0.0;
        
        // Children subdirectories (mapped by folder name)
        std::map<std::string, std::unique_ptr<TreeNode>> subdirs;
        // Immediate child files
        std::vector<TreemapItem> files;
    };

    static HierarchicalTreemapResult compute(
        const std::vector<HierarchicalTreemapItem>& input_items,
        const std::string& root_dir,
        double bounds_x, double bounds_y,
        double bounds_w, double bounds_h,
        double item_padding = 1.5,
        double container_margin = 3.0,
        double header_height = 18.0)
    {
        HierarchicalTreemapResult result;
        if (input_items.empty() || bounds_w <= 0.0 || bounds_h <= 0.0) {
            return result;
        }

        // 1. Build the Directory Tree
        TreeNode root;
        root.name = root_dir;
        root.full_path = root_dir;
        root.depth = 0;

        for (const auto& item : input_items) {
            std::string rel_path = item.filepath;
            if (!root_dir.empty() && rel_path.rfind(root_dir, 0) == 0) {
                rel_path = rel_path.substr(root_dir.size());
                if (!rel_path.empty() && (rel_path.front() == '/' || rel_path.front() == '\\')) {
                    rel_path.erase(0, 1);
                }
            }

            TreeNode* cur = &root;
            std::string cur_accum_path = root_dir;
            
            size_t start = 0;
            size_t slash = rel_path.find_first_of("/\\");
            while (slash != std::string::npos) {
                std::string segment = rel_path.substr(start, slash - start);
                if (!segment.empty()) {
                    if (!cur_accum_path.empty() && cur_accum_path.back() != '/' && cur_accum_path.back() != '\\') {
                        cur_accum_path += "/";
                    }
                    cur_accum_path += segment;

                    auto it = cur->subdirs.find(segment);
                    if (it == cur->subdirs.end()) {
                        auto newNode = std::make_unique<TreeNode>();
                        newNode->name = segment;
                        newNode->full_path = cur_accum_path;
                        newNode->depth = cur->depth + 1;
                        it = cur->subdirs.emplace(segment, std::move(newNode)).first;
                    }
                    cur = it->second.get();
                }
                start = slash + 1;
                slash = rel_path.find_first_of("/\\", start);
            }

            cur->files.push_back(TreemapItem{item.id, std::max(1e-9, item.weight), item.aspect_ratio});
        }

        // 2. Aggregate subtree weights recursively
        auto compute_weights = [](auto& self, TreeNode* node) -> double {
            double sum = 0.0;
            for (const auto& f : node->files) {
                sum += f.weight;
            }
            for (auto& pair : node->subdirs) {
                sum += self(self, pair.second.get());
            }
            node->total_weight = sum;
            return sum;
        };
        compute_weights(compute_weights, &root);

        // 3. Layout directory nodes recursively
        auto layout_node = [&](auto& self, TreeNode* node, double bx, double by, double bw, double bh) -> void {
            if (bw <= 0.0 || bh <= 0.0) return;

            // If not root, record container box
            if (node->depth > 0) {
                result.container_boxes.push_back(ContainerBox{
                    node->full_path,
                    node->name,
                    node->depth,
                    bx, by, bw, bh
                });
            }

            // Calculate inner available region for children
            double pad = (node->depth > 0) ? container_margin : 0.0;
            double hdr = 0.0;
            // Only allocate header space if container is large enough to show a title
            if (node->depth > 0 && bh >= 36.0 && bw >= 60.0) {
                hdr = header_height;
            }

            double inner_x = bx + pad;
            double inner_y = by + pad + hdr;
            double inner_w = std::max(0.0, bw - 2.0 * pad);
            double inner_h = std::max(0.0, bh - 2.0 * pad - hdr);

            if (inner_w <= 0.0 || inner_h <= 0.0) return;

            // Build partition list of immediate children (files and subdirectories)
            struct ChildRef {
                bool is_subdir;
                TreeNode* subdir_ptr;
                size_t file_id;
            };
            std::vector<ChildRef> child_refs;
            std::vector<TreemapItem> partition_items;

            for (const auto& f : node->files) {
                size_t idx = child_refs.size();
                child_refs.push_back(ChildRef{false, nullptr, f.id});
                partition_items.push_back(TreemapItem{idx, f.weight, f.aspect_ratio});
            }

            for (auto& pair : node->subdirs) {
                if (pair.second->total_weight > 0.0) {
                    size_t idx = child_refs.size();
                    child_refs.push_back(ChildRef{true, pair.second.get(), 0});
                    partition_items.push_back(TreemapItem{idx, pair.second->total_weight, 1.0});
                }
            }

            if (partition_items.empty()) return;

            // Compute squarified layout for children in inner box
            std::vector<TreemapBox> child_boxes = SquarifiedTreemap::compute(
                partition_items,
                inner_x, inner_y, inner_w, inner_h,
                0.0
            );

            for (const auto& cbox : child_boxes) {
                if (cbox.id >= child_refs.size()) continue;
                const auto& ref = child_refs[cbox.id];
                if (ref.is_subdir) {
                    self(self, ref.subdir_ptr, cbox.x, cbox.y, cbox.w, cbox.h);
                } else {
                    double fx = cbox.x;
                    double fy = cbox.y;
                    double fw = cbox.w;
                    double fh = cbox.h;
                    if (item_padding > 0.0) {
                        double half = item_padding / 2.0;
                        if (fw > item_padding && fh > item_padding) {
                            fx += half;
                            fy += half;
                            fw -= item_padding;
                            fh -= item_padding;
                        }
                    }
                    result.boxes.push_back(TreemapBox{
                        ref.file_id,
                        fx, fy, fw, fh
                    });
                }
            }
        };

        layout_node(layout_node, &root, bounds_x, bounds_y, bounds_w, bounds_h);
        return result;
    }
};

