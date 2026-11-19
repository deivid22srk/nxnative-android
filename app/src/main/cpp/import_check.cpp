#include "import_check.h"

#include <cstdio>
#include <sstream>

namespace nx2a {
namespace {

std::string escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

}  // namespace

std::string analyzeImport(const NspInfo& info, const KeySet& keys) {
    std::ostringstream j;
    j << '{';
    j << "\"ok\":" << (info.ok ? "true" : "false") << ',';
    j << "\"code\":\"" << escape(info.code) << "\",";
    j << "\"error\":\"" << escape(info.error) << "\",";

    if (info.ok) {
        j << "\"titleId\":\"" << escape(info.titleId) << "\",";
        j << "\"keyGeneration\":" << info.keyGeneration << ',';
        j << "\"ticket\":\"" << escape(info.ticketName) << "\",";
        j << "\"size\":" << info.fileSize << ',';

        j << "\"entries\":[";
        for (std::size_t i = 0; i < info.entries.size(); ++i) {
            if (i) j << ',';
            const NspEntry& e = info.entries[i];
            j << "{\"name\":\"" << escape(e.name) << "\",\"kind\":\"" << escape(e.kind)
              << "\",\"size\":" << e.size << '}';
        }
        j << "],";

        const std::vector<std::string> missing = keys.missingForGeneration(info.keyGeneration);
        j << "\"missingKeys\":[";
        for (std::size_t i = 0; i < missing.size(); ++i) {
            if (i) j << ',';
            j << '"' << escape(missing[i]) << '"';
        }
        j << "],";
        j << "\"keysCount\":" << keys.size() << ',';
        j << "\"keysMaxGeneration\":" << keys.maxGeneration("master_key") << ',';
        j << "\"supported\":" << (missing.empty() ? "true" : "false");
    } else {
        // Keep the shape stable for the Kotlin parser.
        j << "\"titleId\":\"\",\"keyGeneration\":-1,\"size\":" << info.fileSize
          << ",\"entries\":[],\"missingKeys\":[],\"keysCount\":" << keys.size()
          << ",\"keysMaxGeneration\":" << keys.maxGeneration("master_key")
          << ",\"supported\":false";
    }

    j << '}';
    return j.str();
}

}  // namespace nx2a
