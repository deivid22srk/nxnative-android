// Proves that the supplied prod.keys can unlock the container: the ticket's
// title key is decrypted with titlekek_<gen> (AES-ECB) and the program NCA
// header with header_key (AES-XTS). Only derived, non-secret facts leave this
// module (booleans, the NCA magic, a content type and the title id).
#pragma once

#include <string>

#include "keys.h"

namespace nx2a {

struct ContentCheck {
    bool ok = false;
    std::string code;        // OK | ERR_NSP | ERR_KEY_* | ERR_TICKET | ERR_NCA | ERR_IO
    std::string error;       // English fallback detail (no secrets)
    bool titleKeyOk = false;
    bool headerOk = false;
    bool headerPlaintext = false;
    std::string headerMagic;
    int contentType = -1;
    std::string titleId;     // 16 uppercase hex chars, from the ticket
    bool titleIdInHeader = false;
    std::string titleKeyName;   // key name used, never the value
    std::string headerKeyName;
};

ContentCheck checkContentKeys(const std::string& nspPath, const KeySet& keys);

std::string contentCheckJson(const ContentCheck& check);

}  // namespace nx2a