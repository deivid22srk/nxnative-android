// prod.keys / title.keys reader.
//
// Values are secrets: they stay in this object and are never printed,
// copied into a result string or written anywhere. Only key *names* and
// counts leave this module.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nx2a {

class KeySet {
public:
    // Parses `name = hex` lines. Returns false and fills *error with a line
    // number and reason (never the value). `path` is only used in errors.
    bool loadFile(const std::string& path, std::string* error);
    bool loadText(const std::string& text, std::string* error);

    bool has(const std::string& name) const;
    // Internal use only: copies the bytes of `name`. The JNI surface never
    // exposes this, and values are never printed or written to disk.
    bool get(const std::string& name, std::vector<std::uint8_t>* out) const;
    std::size_t size() const { return values_.size(); }

    // Highest NN seen for "<prefix>_NN", or -1 when the prefix is absent.
    int maxGeneration(const std::string& prefix) const;

    // Names required to open an NCA/ticket of this key generation. Missing
    // ones only; empty when everything is present.
    std::vector<std::string> missingForGeneration(int generation) const;

private:
    std::map<std::string, std::vector<std::uint8_t>> values_;
};

// "0x0e" / 14 -> "0e"
std::string generationSuffix(int generation);

}  // namespace nx2a
