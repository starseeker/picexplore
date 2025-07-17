#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <errno.h>
#include "svg.hpp"

extern "C" {
#define JRL_IMPLEMENTATION
#include "justified_layout.h"
}

int main(int argc, const char *argv[])
{
    std::chrono::time_point<std::chrono::steady_clock> stime;
    std::chrono::time_point<std::chrono::steady_clock> etime;

    if (argc % 2 != 1) {
	std::cerr << "Format is: jltest x1 y1 [x2 y2]...\n";
	return -1;
    }

    stime = std::chrono::steady_clock::now();

    std::vector<size_t> sizes;
    char *end;

    for (int i = 1; i < argc; i++) {
	size_t n;
	long l = strtol(argv[i], &end, 10);
	if (end == argv[i]) {
	    std::cerr << "Argument " << i << " (" << argv[i] << ") is not a valid number";
	    return -1;
	} else if ('\0' != *end) {
	    std::cerr << "Argument " << i << " (" << argv[i] << ") has extra characters at end of input: " << end << "\n";
	    return -1;
	} else if ((LONG_MIN == l || LONG_MAX == l) && ERANGE == errno) {
	    std::cerr << argv[i] << "out of range of type long\n";
	    return -1;
	}
        if (l < 0) {
	    std::cerr << argv[i] << "negative values are not supported\n";
	    return -1;
	}
	unsigned long ul = (unsigned long)l;
	if (ul > std::numeric_limits<std::size_t>::max()) {
	    std::cerr << argv[i] << "out of range of type size_t\n";
	    return -1;
	} else if (ul < std::numeric_limits<std::size_t>::min()) {
	    std::cerr << argv[i] << " less than size_t MAX\n";
	    return -1;
	}
	n = (size_t)ul;
	sizes.push_back(n);
    }

    struct jrl_config jrl_cfg = JRL_CONFIG_INIT_DEFAULT;
    struct jrl_layout *jrl;
    jrl_cfg.obj_cnt = sizes.size() / 2;
    jrl_cfg.sizes = sizes.data();
    int jrl_ret = jlayout(&jrl, &jrl_cfg);

    etime = std::chrono::steady_clock::now();
    std::cout << "Layout time:" << std::chrono::duration_cast<std::chrono::seconds>(etime - stime).count() << " seconds.\n";

    if (jrl_ret != JRL_SUCCESS) {
	std::cerr << "Justified image layout failed\n";
	return -1;
    }

    // Report layout
    svg::Dimensions d(jrl_cfg.containerWidth, jrl->containerHeight);
    svg::Document svg_out("jrl.svg", svg::Layout(d, svg::Layout::TopLeft));
    for (size_t i = 0; i < jrl->row_count; i++) {
	struct jrl_row *r = jrl->rows[i];
	std::cout << "Row " << i << ":\n";
	std::cout << "  Item count: " << r->item_count << ":\n";
	std::cout << "  Pos(top, left, width, height): " << r->top << "," << r->left << "," << r->width << "," << r->height << "\n";
	for (size_t j = 0; j < r->item_count; j++) {
	    struct jrl_layoutItem *item = r->items[j];
	    std::cout << "     Item " << j << "(top, left, width, height): " << item->top << "," << item->left << "," << item->width << "," << item->height << "\n";
	    svg_out << svg::Rectangle(svg::Point(item->left, item->top), item->width, item->height, svg::Color::Yellow);
	}
    }
    svg_out.save();

    return 0;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
