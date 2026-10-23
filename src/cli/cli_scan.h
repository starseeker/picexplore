#pragma once

#include <string>
#include "../utils.h"

int run_headless_scan(const std::string& directory, const std::string& db_path, bool verbose, bool recursive = true);
int run_headless_scan(const InputSelection& selection, const std::string& db_path, bool verbose);
