// Exact rooted-star orbit tables. Every exclusion is a necessary local-link
// condition, not a heuristic. Canonical colors are packed into integers.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <unistd.h>

using Mask = uint32_t;
using Color = uint64_t;
using Perm = std::vector<unsigned char>;
std::array<std::vector<Perm>, 9> permutations;
int pc(Mask x) { return __builtin_popcount(x); }
Color ones(int n) { return (Color(1) << n) - 1; }

const std::vector<Perm>& maps(int s) {
    if (s < 1 || s > 8) throw std::runtime_error("Unsupported star degree");
    auto& result = permutations[s];
    if (!result.empty()) return result;
    std::vector<int> p(s);
    std::iota(p.begin(), p.end(), 0);
    do {
        Perm mapping(1 << s, 0);
        for (int m = 0; m < (1 << s); ++m)
            for (int i = 0; i < s; ++i)
                if ((m >> i) & 1) mapping[m] |= 1 << p[i];
        result.push_back(std::move(mapping));
    } while (std::next_permutation(p.begin(), p.end()));
    return result;
}

std::vector<Mask> decode(const std::string& line, int n, int k) {
    if (line.empty()) throw std::runtime_error("Empty graph6");
    int order = (unsigned char)line[0] - 63;
    int r = order - n;
    if (r < 1 || order >= 63 || line.size() != 1u + (order*(order-1)/2+5)/6)
        throw std::runtime_error("Invalid graph6 length");
    std::vector<Mask> b(r);
    int bit = 0;
    for (int j = 1; j < order; ++j) for (int i = 0; i < j; ++i, ++bit) {
        int code = (unsigned char)line[1+bit/6] - 63;
        if (code < 0 || code > 63) throw std::runtime_error("Invalid graph6 byte");
        if (!((code >> (5-bit%6)) & 1)) continue;
        if (!(i < r && r <= j)) throw std::runtime_error("Invalid bipartition");
        b[i] |= Mask(1) << (j-r);
    }
    for (Mask a : b) if (pc(a) != k) throw std::runtime_error("Wrong block size");
    return b;
}

void validate_catalog(const std::vector<std::vector<Mask>>& links, int n) {
    if (links.empty()) throw std::runtime_error("Empty catalogue");
    for (const auto& link : links) {
        if (link.size() != links.front().size()) throw std::runtime_error("Nonuniform link size");
        auto sorted = link;
        std::sort(sorted.begin(),sorted.end());
        if (std::adjacent_find(sorted.begin(),sorted.end()) != sorted.end()) throw std::runtime_error("Duplicate link block");
        for (int p = 0; p < n; ++p) for (int q = p+1; q < n; ++q) {
            Mask pair = (Mask(1) << p) | (Mask(1) << q);
            bool covered = false;
            for (Mask b : link) if ((b & pair) == pair) { covered = true; break; }
            if (!covered) throw std::runtime_error("Uncovered link pair");
        }
    }
}

struct Group { Mask points; int size; };
struct Star {
    int s;
    std::string signature;
    std::vector<std::vector<Group>> actions;
    Star(const std::vector<Mask>& blocks, int root, int n) {
        std::vector<Mask> rows;
        for (Mask b : blocks) if ((b >> root) & 1) rows.push_back(b);
        s = rows.size();
        std::vector<int> points;
        std::vector<unsigned char> columns;
        for (int p = 0; p < n; ++p) if (p != root) {
            unsigned char col = 0;
            for (size_t j = 0; j < rows.size(); ++j) if ((rows[j] >> p) & 1) col |= 1 << j;
            points.push_back(p);
            columns.push_back(col);
        }
        const auto& all = maps(s);
        std::vector<size_t> canonical;
        for (size_t q = 0; q < all.size(); ++q) {
            std::string key;
            for (auto col : columns) key += char(all[q][col]);
            std::sort(key.begin(), key.end(), [](char a, char b) { return (unsigned char)a < (unsigned char)b; });
            if (canonical.empty() || key < signature) { signature = key; canonical = {q}; }
            else if (key == signature) canonical.push_back(q);
        }
        for (size_t q : canonical) {
            std::array<Mask, 256> grouped{};
            for (size_t j = 0; j < points.size(); ++j) grouped[all[q][columns[j]]] |= Mask(1) << points[j];
            std::vector<Group> groups;
            for (Mask group : grouped) if (group) groups.push_back({group, pc(group)});
            actions.push_back(std::move(groups));
        }
    }
    Color key(Mask marked) const {
        Color best = UINT64_MAX;
        for (const auto& action : actions) {
            Color value = 0;
            for (const auto& g : action) value = (value << g.size) | ones(pc(g.points & marked));
            best = std::min(best, value);
        }
        return best;
    }
    Color pairkey(Mask a, Mask b) const {
        Color best = UINT64_MAX;
        for (const auto& action : actions) {
            Color direct = 0, swapped = 0;
            for (const auto& g : action) {
                int both = pc(g.points & a & b);
                int only_a = pc(g.points & a & ~b);
                int only_b = pc(g.points & b & ~a);
                Color tail = ones(2*both);
                Color ac = ones(2*only_a)/3, bc = ones(2*only_b)/3;
                Color code = (ac << (2*(only_b+both))) | ((2*bc) << (2*both)) | tail;
                Color reverse = (bc << (2*(only_a+both))) | ((2*ac) << (2*both)) | tail;
                direct = (direct << (2*g.size)) | code;
                swapped = (swapped << (2*g.size)) | reverse;
            }
            best = std::min(best, std::min(direct, swapped));
        }
        return best;
    }
};
struct Table { std::unordered_set<Color> single, pairs; };
using Tables = std::array<std::unordered_map<std::string, Table>, 9>;

void put(std::ostream& out, uint64_t value) {
    for (int j = 0; j < 8; ++j) out.put(char(value >> (8*j)));
}
uint64_t get(std::istream& in) {
    uint64_t value = 0;
    for (int j = 0; j < 8; ++j) {
        int byte = in.get();
        if (byte < 0) throw std::runtime_error("Truncated cache");
        value |= uint64_t(byte) << (8*j);
    }
    return value;
}

int main(int argc, char** argv) {
    try {
        if (argc != 7 && argc != 8 && argc != 9) throw std::runtime_error("local_filter links n link_k start|@indices stop output [cache [admissible-indices]]");
        int n = std::stoi(argv[2]), k = std::stoi(argv[3]);
        if (n < 3 || n > 20 || k < 2 || k >= n) throw std::runtime_error("Invalid parameters");
        std::vector<int> indices;
        if (argv[4][0] == '@') {
            std::ifstream file(std::string(argv[4]).substr(1));
            int index;
            while (file >> index) indices.push_back(index);
        } else {
            int start = std::stoi(argv[4]), stop = std::stoi(argv[5]);
            for (int index = start; index < stop; ++index) indices.push_back(index);
        }
        if (indices.empty()) throw std::runtime_error("Empty class list");
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("Cannot read catalogue");
        std::vector<std::vector<Mask>> links;
        std::string line;
        while (std::getline(input, line)) links.push_back(decode(line, n, k));
        validate_catalog(links,n);
        std::vector<int> admissible;
        if (argc == 9) {
            std::ifstream selected(argv[8]);
            if (!selected) throw std::runtime_error("Cannot read admissible indices");
            int index;
            while (selected >> index) {
                if (index < 0 || index >= int(links.size())) throw std::runtime_error("Admissible index out of range");
                admissible.push_back(index);
            }
            if (!selected.eof() || admissible.empty()) throw std::runtime_error("Invalid admissible list");
        } else {
            admissible.resize(links.size());
            std::iota(admissible.begin(), admissible.end(), 0);
        }
        for (int index : indices) if (index < 0 || index >= int(links.size())) throw std::runtime_error("Class out of range");
        Tables tables;
        auto begin = std::chrono::steady_clock::now();
        bool cached = false;
        std::string header = "star-colors " + std::to_string(n) + " " + std::to_string(k);
        if (argc == 9) {
            header += " admissible";
            for (int index : admissible) header += " " + std::to_string(index);
        }
        if (argc >= 8) {
            std::ifstream cache(argv[7], std::ios::binary);
            if (cache) {
                std::string found;
                std::getline(cache, found);
                if (found != header) throw std::runtime_error("Invalid cache header");
                for (int s = 1; s <= 8; ++s) {
                    uint64_t count = get(cache);
                    if (count > 1000000) throw std::runtime_error("Invalid cache count");
                    for (uint64_t j = 0; j < count; ++j) {
                        std::string signature(n-1, '\0');
                        cache.read(signature.data(), signature.size());
                        Table& t = tables[s][signature];
                        for (int size = 1; size <= 2; ++size) {
                            auto& values = size == 1 ? t.single : t.pairs;
                            uint64_t length = get(cache);
                            if (length > 100000000) throw std::runtime_error("Invalid color count");
                            for (uint64_t z = 0; z < length; ++z) values.insert(get(cache));
                        }
                    }
                }
                cached = true;
            }
        }
        if (!cached) {
            for (int index : admissible) for (int p = 0; p < n; ++p) {
                const auto& link = links[index];
                Star star(link, p, n);
                Table& t = tables[star.s][star.signature];
                std::vector<Mask> outside;
                for (Mask b : link) if (!((b >> p) & 1)) { t.single.insert(star.key(b)); outside.push_back(b); }
                for (size_t i = 0; i < outside.size(); ++i)
                    for (size_t j = i+1; j < outside.size(); ++j) t.pairs.insert(star.pairkey(outside[i], outside[j]));
            }
            if (argc >= 8) {
                std::string temporary = std::string(argv[7]) + "." + std::to_string(getpid());
                std::ofstream cache(temporary, std::ios::binary);
                cache << header << "\n";
                for (int s = 1; s <= 8; ++s) {
                    std::vector<std::string> signatures;
                    for (const auto& item : tables[s]) signatures.push_back(item.first);
                    std::sort(signatures.begin(), signatures.end());
                    put(cache, signatures.size());
                    for (const auto& signature : signatures) {
                        cache.write(signature.data(), signature.size());
                        const Table& t = tables[s].at(signature);
                        for (int size = 1; size <= 2; ++size) {
                            const auto& values = size == 1 ? t.single : t.pairs;
                            std::vector<Color> sorted(values.begin(), values.end());
                            std::sort(sorted.begin(), sorted.end());
                            put(cache, sorted.size());
                            for (Color value : sorted) put(cache, value);
                        }
                    }
                }
                cache.close();
                if (!cache) throw std::runtime_error("Cannot write cache");
                std::filesystem::rename(temporary, argv[7]);
            }
        }
        std::cerr << "Catalogue " << (cached ? "loaded" : "built") << " in "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count() << " s\n";
        std::ofstream out(argv[6]);
        if (!out) throw std::runtime_error("Cannot write output");
        for (int index : indices) {
            const auto& link = links[index];
            if (std::find(admissible.begin(), admissible.end(), index) == admissible.end())
                throw std::runtime_error("Fixed class absent from admissible list");
            std::vector<Star> stars;
            std::vector<const Table*> local;
            for (int p = 0; p < n; ++p) {
                stars.emplace_back(link, p, n);
                local.push_back(&tables[stars.back().s].at(stars.back().signature));
            }
            std::vector<int> order(n);
            std::iota(order.begin(), order.end(), 0);
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return stars[a].actions.size() < stars[b].actions.size(); });
            std::vector<Mask> candidates;
            for (Mask b = 0; b < (Mask(1) << n); ++b) if (pc(b) == k+1) {
                bool ok = true;
                for (int p : order) {
                    if ((b >> p) & 1) ok = local[p]->single.count(stars[p].key(b));
                    if (!ok) break;
                }
                if (ok) candidates.push_back(b);
            }
            out << "{\"class\":" << index << ",\"allowed\":[";
            for (size_t i = 0; i < candidates.size(); ++i) out << (i ? "," : "") << candidates[i];
            out << "],\"incompatible\":[";
            bool first = true;
            uint64_t bad = 0;
            for (size_t i = 0; i < candidates.size(); ++i) for (size_t j = i+1; j < candidates.size(); ++j) {
                Mask common = candidates[i] & candidates[j];
                bool ok = true;
                for (int p : order) {
                    if ((common >> p) & 1) ok = local[p]->pairs.count(stars[p].pairkey(candidates[i], candidates[j]));
                    if (!ok) break;
                }
                if (!ok) {
                    out << (first ? "" : ",") << "[" << i+1 << "," << j+1 << "]";
                    first = false;
                    ++bad;
                }
            }
            out << "]}\n";
            if (!out) throw std::runtime_error("Cannot write class result");
            if (indices.size() < 10 || index%1000 == 0) std::cerr << index << " candidates " << candidates.size() << " incompatible " << bad << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    return 0;
}
