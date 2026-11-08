#include "parser.h"

Registry& Registry::instance() {
    static Registry r;
    return r;
}

const Format* Registry::best(const Bytes& data) const {
    const Format* best = nullptr;
    int bestScore = 0;
    for (const auto& f : formats_) {
        int s = f.probe(data);
        if (s > bestScore) { bestScore = s; best = &f; }
    }
    return best;
}
