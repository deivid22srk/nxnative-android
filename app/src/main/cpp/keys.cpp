#include "keys.h"

#include <cstdio>
#include <cstring>

namespace nx2a {
namespace {

std::string trim(const std::string& in) {
    std::size_t a = 0, b = in.size();
    while (a < b && (in[a] == ' ' || in[a] == '\t' || in[a] == '\r' || in[a] == '\n')) ++a;
    while (b > a && (in[b - 1] == ' ' || in[b - 1] == '\t' || in[b - 1] == '\r' || in[b - 1] == '\n')) --b;
    return in.substr(a, b - a);
}

bool validName(const std::string& s) {
    if (s.empty() || s.size() > 96) return false;
    for (char c : s) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseHex(const std::string& s, std::vector<std::uint8_t>* out) {
    if (s.empty() || (s.size() % 2) != 0) return false;
    out->clear();
    out->reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2) {
        int hi = hexNibble(s[i]);
        int lo = hexNibble(s[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out->push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

}  // namespace

bool KeySet::loadText(const std::string& text, std::string* error) {
    std::size_t pos = 0;
    int lineNo = 0;
    while (pos <= text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            if (error) *error = "linha " + std::to_string(lineNo) + ": falta '='";
            return false;
        }
        std::string name = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        if (!validName(name)) {
            if (error) *error = "linha " + std::to_string(lineNo) + ": nome de chave inválido";
            return false;
        }
        // Values may carry a leading 0x in some dumps.
        if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
            value = value.substr(2);
        }
        std::vector<std::uint8_t> bytes;
        if (!parseHex(value, &bytes) || bytes.size() < 16) {
            if (error) *error = "linha " + std::to_string(lineNo) + ": valor hex inválido (esperado >= 16 bytes)";
            return false;
        }
        values_[name] = std::move(bytes);
    }
    if (values_.empty()) {
        if (error) *error = "nenhuma chave encontrada";
        return false;
    }
    return true;
}

bool KeySet::loadFile(const std::string& path, std::string* error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (error) *error = "não foi possível abrir o arquivo de chaves";
        return false;
    }
    std::string text;
    char buf[8192];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    std::fclose(f);
    return loadText(text, error);
}

bool KeySet::has(const std::string& name) const {
    return values_.find(name) != values_.end();
}

int KeySet::maxGeneration(const std::string& prefix) const {
    int best = -1;
    const std::string head = prefix + "_";
    for (const auto& kv : values_) {
        const std::string& name = kv.first;
        if (name.size() != head.size() + 2) continue;
        if (name.compare(0, head.size(), head) != 0) continue;
        int hi = hexNibble(name[head.size()]);
        int lo = hexNibble(name[head.size() + 1]);
        if (hi < 0 || lo < 0) continue;
        int v = (hi << 4) | lo;
        if (v > best) best = v;
    }
    return best;
}

std::vector<std::string> KeySet::missingForGeneration(int generation) const {
    std::vector<std::string> missing;
    const std::string gen = generationSuffix(generation);
    if (!has("header_key")) missing.push_back("header_key");
    if (!has("master_key_" + gen)) missing.push_back("master_key_" + gen);
    if (!has("titlekek_" + gen)) missing.push_back("titlekek_" + gen);
    if (!has("key_area_key_application_" + gen)) {
        missing.push_back("key_area_key_application_" + gen);
    }
    return missing;
}

std::string generationSuffix(int generation) {
    static const char* digits = "0123456789abcdef";
    std::string s(2, '0');
    s[0] = digits[(generation >> 4) & 0xF];
    s[1] = digits[generation & 0xF];
    return s;
}

}  // namespace nx2a
