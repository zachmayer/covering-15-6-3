// Uniform suffix preprocessing. The rank tables are built once, in memory.
// A color (or complete rooted link) is admitted iff its greatest supporting
// class rank is >= the fixed root's rank. No previous UNSAT results are used.
#define main local_filter_program_main
#include "local_filter.cpp"
#undef main
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <cstring>
#include <cstdlib>

struct RankedTable {
    std::unordered_map<Color, int> single, pairs;
    std::map<std::vector<Mask>, int> complete;
};
using RankedTables = std::array<std::unordered_map<std::string, RankedTable>, 9>;

struct Uniform {
    int n, k;
    std::vector<std::vector<Mask>> links;
    std::vector<int> order, ranks;
    RankedTables tables;

    Uniform(const char* catalogue, int n_, int k_, const char* ordering): n(n_), k(k_) {
        if (n < 3 || n > 20 || k < 2 || k >= n) throw std::runtime_error("Invalid parameters");
        std::ifstream file(catalogue);
        if (!file) throw std::runtime_error("Cannot read catalogue");
        std::string line;
        while (std::getline(file, line)) links.push_back(decode(line,n,k));
        validate_catalog(links,n);
        std::ifstream ordered(ordering);
        if (!ordered) throw std::runtime_error("Cannot read order");
        int index;
        while (ordered >> index) order.push_back(index);
        if (!ordered.eof() || order.size() != links.size()) throw std::runtime_error("Wrong order length");
        ranks.assign(links.size(),-1);
        for (size_t rank = 0; rank < order.size(); ++rank) {
            index = order[rank];
            if (index < 0 || index >= int(links.size()) || ranks[index] != -1)
                throw std::runtime_error("Order is not a permutation");
            ranks[index] = rank;
        }
        // Ascending traversal makes assignment exactly the maximum support rank.
        for (size_t rank = 0; rank < order.size(); ++rank) {
            const auto& link = links[order[rank]];
            for (int p = 0; p < n; ++p) {
                Star star(link,p,n);
                auto& table = tables[star.s][star.signature];
                std::vector<Mask> outside;
                for (Mask b : link) if (!((b >> p) & 1)) {
                    table.single[star.key(b)] = rank;
                    outside.push_back(b);
                }
                for (size_t i = 0; i < outside.size(); ++i)
                    for (size_t j = i+1; j < outside.size(); ++j)
                        table.pairs[star.pairkey(outside[i],outside[j])] = rank;
                std::vector<int> point_to_slot(n,-1);
                int slot = 0;
                for (const Group& group : star.actions.front())
                    for (int q = 0; q < n; ++q) if ((group.points >> q) & 1) point_to_slot[q] = slot++;
                std::vector<Mask> normalized;
                for (Mask b : outside) {
                    Mask mapped = 0;
                    for (int q = 0; q < n; ++q) if ((b >> q) & 1) mapped |= Mask(1) << point_to_slot[q];
                    normalized.push_back(mapped);
                }
                std::sort(normalized.begin(),normalized.end());
                table.complete[normalized] = rank;
            }
        }
    }

    std::string prepare(int index, int forced_root = -1, bool final_pairs = true,
                        int* viable_count = nullptr) const {
        if (index < 0 || index >= int(links.size())) throw std::runtime_error("Class out of range");
        int rank = ranks[index];
        const auto& fixed = links[index];
        std::vector<Star> stars;
        std::vector<const RankedTable*> local;
        for (int p = 0; p < n; ++p) {
            stars.emplace_back(fixed,p,n);
            local.push_back(&tables[stars.back().s].at(stars.back().signature));
        }
        auto admitted = [rank](const auto& table, Color key) {
            auto found = table.find(key);
            return found != table.end() && found->second >= rank;
        };
        std::vector<int> points(n);
        std::iota(points.begin(),points.end(),0);
        std::stable_sort(points.begin(),points.end(),[&](int a, int b) { return stars[a].actions.size() < stars[b].actions.size(); });
        std::vector<Mask> candidates;
        for (Mask b = 0; b < (Mask(1) << n); ++b) if (pc(b) == k+1) {
            bool ok = true;
            for (int p : points) if ((b >> p) & 1) {
                if (!admitted(local[p]->single,stars[p].key(b))) { ok = false; break; }
            }
            if (ok) candidates.push_back(b);
        }
        int count = candidates.size(), words = (count+63)/64;
        std::vector<int> variable(Mask(1) << n,0);
        for (int i = 0; i < count; ++i) variable[candidates[i]] = i+1;
        // 0 = unknown, 1 = compatible, 2 = incompatible. Compute only pairs
        // needed by an embedding first; empty option sets need no other pairs.
        std::vector<uint8_t> pair_state(size_t(count)*count,0);
        std::vector<std::vector<uint64_t>> conflict(count,std::vector<uint64_t>(words,0));
        std::vector<std::pair<int,int>> incompatible;
        auto compatible = [&](int i, int j) {
            uint8_t known = pair_state[size_t(i)*count+j];
            if (known) return known == 1;
            Mask common = candidates[i] & candidates[j];
            bool ok = true;
            for (int p : points) if ((common >> p) & 1) {
                if (!admitted(local[p]->pairs,stars[p].pairkey(candidates[i],candidates[j]))) { ok = false; break; }
            }
            pair_state[size_t(i)*count+j] = pair_state[size_t(j)*count+i] = ok ? 1 : 2;
            if (!ok) {
                conflict[i][j/64] |= uint64_t(1) << (j%64);
                conflict[j][i/64] |= uint64_t(1) << (i%64);
            }
            return ok;
        };
        int r = fixed.size(), root = 0;
        std::vector<int> degrees(n,0);
        for (Mask b : fixed) for (int p = 0; p < n; ++p) degrees[p] += (b >> p) & 1;
        for (int p = 1; p < n; ++p) if (degrees[p] > degrees[root]) root = p;
        if (forced_root >= 0) {
            if (forced_root >= n) throw std::runtime_error("Point out of range");
            root = forced_root;
        }
        const Star& target = stars[root];
        std::vector<std::vector<Mask>> normalized;
        for (const auto& item : local[root]->complete) if (item.second >= rank) normalized.push_back(item.first);
        std::set<std::vector<int>> options;
        uint64_t embeddings = 0;
        for (const auto& action : target.actions) {
            std::vector<Mask> slot_to_point(n-1,0);
            std::function<void(size_t,int)> enumerate = [&](size_t group_index,int offset) {
                if (group_index != action.size()) {
                    const Group& group = action[group_index];
                    std::vector<int> points;
                    for (int p = 0; p < n; ++p) if ((group.points >> p) & 1) points.push_back(p);
                    do {
                        for (size_t j = 0; j < points.size(); ++j) slot_to_point[offset+j] = Mask(1) << points[j];
                        enumerate(group_index+1,offset+points.size());
                    } while (std::next_permutation(points.begin(),points.end()));
                    return;
                }
                for (const auto& outside : normalized) {
                    ++embeddings;
                    std::vector<int> chosen;
                    std::vector<int> used(n,0);
                    bool ok = true;
                    for (Mask row : outside) {
                        Mask mapped = Mask(1) << root;
                        for (int slot = 0; slot < n-1; ++slot) if ((row >> slot) & 1) mapped |= slot_to_point[slot];
                        int id = variable[mapped];
                        if (!id) { ok = false; break; }
                        for (int previous : chosen) if (!compatible(previous-1,id-1)) { ok = false; break; }
                        if (!ok) break;
                        for (int p = 0; p < n; ++p) if ((mapped >> p) & 1) {
                            if (++used[p] > r-degrees[p]) { ok = false; break; }
                        }
                        if (!ok) break;
                        chosen.push_back(id);
                    }
                    if (ok) {
                        std::sort(chosen.begin(),chosen.end());
                        options.insert(std::move(chosen));
                    }
                }
            };
            enumerate(0,0);
        }
        // Forward checks consult conflicts only against blocks in an option.
        std::set<int> option_variables;
        for (const auto& option : options) for (int id : option) option_variables.insert(id-1);
        for (int i : option_variables) for (int j = 0; j < count; ++j) if (i != j) compatible(i,j);
        std::vector<uint64_t> base(words,0);
        std::vector<Mask> subsets;
        for (int p = 0; p < n; ++p) subsets.push_back(Mask(1) << p);
        for (int p = 0; p < n; ++p) for (int q = p+1; q < n; ++q)
            subsets.push_back((Mask(1) << p) | (Mask(1) << q));
        for (int p = 0; p < n; ++p) for (int q = p+1; q < n; ++q) for (int t = q+1; t < n; ++t)
            subsets.push_back((Mask(1) << p) | (Mask(1) << q) | (Mask(1) << t));
        std::vector<std::vector<uint64_t>> covering(subsets.size(),std::vector<uint64_t>(words,0));
        std::vector<int> needs;
        int pair_min = (n-1+k-2)/(k-1);
        for (size_t t = 0; t < subsets.size(); ++t) {
            int size = pc(subsets[t]);
            int need = size == 1 ? r : (size == 2 ? pair_min : 1);
            for (Mask b : fixed) need -= ((b & subsets[t]) == subsets[t]);
            needs.push_back(need);
        }
        for (int i = 0; i < count; ++i) {
            if (!((candidates[i] >> root) & 1)) base[i/64] |= uint64_t(1) << (i%64);
            for (size_t t = 0; t < subsets.size(); ++t) if ((candidates[i] & subsets[t]) == subsets[t])
                covering[t][i/64] |= uint64_t(1) << (i%64);
        }
        std::vector<std::vector<int>> viable;
        int remaining_count = (n+1)*r/(k+1) - r - (r-target.s);
        for (const auto& option : options) {
            std::vector<uint64_t> available = base;
            std::vector<int> residual = needs;
            for (int id : option) {
                for (int w = 0; w < words; ++w) available[w] &= ~conflict[id-1][w];
                for (size_t t = 0; t < subsets.size(); ++t)
                    residual[t] -= ((candidates[id-1] & subsets[t]) == subsets[t]);
            }
            std::vector<int> excess(n,0);
            for (int p = 0; p < n; ++p) excess[p] = std::max(0,degrees[p]-pair_min);
            size_t pair_index = n;
            for (int p = 0; p < n; ++p) for (int q = p+1; q < n; ++q, ++pair_index) {
                int used = std::max(0,-residual[pair_index]);
                excess[p] += used;
                excess[q] += used;
            }
            int excess_budget = r*k-n*pair_min;
            bool budget_ok = true;
            for (int used : excess) if (used > excess_budget) budget_ok = false;
            if (!budget_ok) continue;
            for (int p = 0; p < n; ++p) if (residual[p] == 0)
                for (int w = 0; w < words; ++w) available[w] &= ~covering[p][w];
            int total = 0;
            for (uint64_t word : available) total += __builtin_popcountll(word);
            bool ok = total >= remaining_count;
            for (size_t t = 0; t < subsets.size() && ok; ++t) if (residual[t] > 0) {
                int possible = 0;
                for (int w = 0; w < words && possible < residual[t]; ++w)
                    possible += __builtin_popcountll(available[w] & covering[t][w]);
                if (possible < residual[t]) ok = false;
            }
            if (ok) viable.push_back(option);
        }
        // Only a nonempty disjunction needs the remaining pair clauses.
        if (viable_count) *viable_count = viable.size();
        if (final_pairs && !viable.empty()) for (int i = 0; i < count; ++i) for (int j = i+1; j < count; ++j)
            if (!compatible(i,j)) incompatible.emplace_back(i+1,j+1);
        std::ostringstream out;
        out << "{\"point\":" << root+1 << ",\"source_roots\":" << 0 << ",\"normalized_links\":" << normalized.size()
            << ",\"embeddings\":" << embeddings << ",\"options_before_forward\":" << options.size() << ",\"options\":[";
        bool first = true;
        for (const auto& option : viable) {
            out << (first ? "" : ",") << "[";
            first = false;
            for (size_t j = 0; j < option.size(); ++j) out << (j ? "," : "") << option[j];
            out << "]";
        }
        out << "]}\n";

        std::string star_json = out.str();
        std::ostringstream result;
        result << "{\"class\":" << index << ",\"rank\":" << rank << ",\"allowed\":[";
        for (size_t i = 0; i < candidates.size(); ++i) result << (i ? "," : "") << candidates[i];
        result << "],\"incompatible\":[";
        for (size_t i = 0; i < incompatible.size(); ++i)
            result << (i ? "," : "") << "[" << incompatible[i].first << "," << incompatible[i].second << "]";
        result << "],\"star\":" << star_json << "}";
        return result.str();
    }

    std::string prepare_best(int index) const {
        const auto& fixed = links.at(index);
        std::vector<int> points(n), degrees(n,0);
        std::iota(points.begin(),points.end(),0);
        for (Mask block : fixed) for (int p = 0; p < n; ++p) degrees[p] += (block >> p) & 1;
        std::vector<long double> costs(n,0);
        for (int p = 0; p < n; ++p) {
            Star star(fixed,p,n);
            const auto& complete = tables[star.s].at(star.signature).complete;
            for (const auto& item : complete) costs[p] += item.second >= ranks[index];
            costs[p] *= star.actions.size();
            for (const auto& group : star.actions.front())
                for (int factor = 2; factor <= group.size; ++factor) costs[p] *= factor;
        }
        std::stable_sort(points.begin(),points.end(),[&](int a, int b) { return costs[a] < costs[b]; });
        int best = points.front(), best_count = INT32_MAX;
        // Any second point is sound. Try up to four inexpensive embeddings,
        // stopping at an empty option set; otherwise encode the smallest one.
        int attempts = 0;
        for (int point : points) {
            if (attempts == 4 || costs[point] > 4 * std::max(costs[points.front()],10000.L)) break;
            ++attempts;
            int count = 0;
            std::string result = prepare(index,point,false,&count);
            if (!count) return result;
            if (count < best_count) { best = point; best_count = count; }
        }
        return prepare(index,best,true);
    }

    void write_scores(const char* path) const {
        std::ofstream out(path);
        if (!out) throw std::runtime_error("Cannot write scores");
        for (size_t index = 0; index < links.size(); ++index) {
            const auto& fixed = links[index];
            std::vector<Star> stars;
            std::vector<const RankedTable*> local;
            for (int p = 0; p < n; ++p) {
                stars.emplace_back(fixed,p,n);
                local.push_back(&tables[stars.back().s].at(stars.back().signature));
            }
            std::vector<int> points(n);
            std::iota(points.begin(),points.end(),0);
            std::stable_sort(points.begin(),points.end(),[&](int a, int b) { return stars[a].actions.size() < stars[b].actions.size(); });
            int count = 0;
            for (Mask b = 0; b < (Mask(1) << n); ++b) if (pc(b) == k+1) {
                bool ok = true;
                for (int p : points) if ((b >> p) & 1) {
                    if (!local[p]->single.count(stars[p].key(b))) { ok = false; break; }
                }
                count += ok;
            }
            out << index << " " << count << "\n";
        }
        if (!out) throw std::runtime_error("Cannot write scores");
    }
};

// Each handle is immutable after construction. Calls may run on two threads.
// All star permutation maps needed by prepare have been initialized above.
thread_local std::string uniform_error;
extern "C" {
const char* uniform_last_error() { return uniform_error.c_str(); }
void* uniform_open(const char* catalogue, int n, int k, const char* ordering) {
    try { return new Uniform(catalogue,n,k,ordering); }
    catch (const std::exception& e) { uniform_error = e.what(); return nullptr; }
}
void uniform_close(void* handle) { delete static_cast<Uniform*>(handle); }
char* uniform_prepare(void* handle, int index) {
    try {
        std::string value = static_cast<Uniform*>(handle)->prepare(index);
        char* result = static_cast<char*>(std::malloc(value.size()+1));
        if (!result) throw std::bad_alloc();
        std::memcpy(result,value.c_str(),value.size()+1);
        return result;
    } catch (const std::exception& e) { uniform_error = e.what(); return nullptr; }
}
void uniform_free(char* value) { std::free(value); }
int uniform_scores(void* handle, const char* path) {
    try { static_cast<Uniform*>(handle)->write_scores(path); return 0; }
    catch (const std::exception& e) { uniform_error = e.what(); return -1; }
}
char* uniform_prepare_best(void* handle, int index) {
    try {
        std::string value = static_cast<Uniform*>(handle)->prepare_best(index);
        char* result = static_cast<char*>(std::malloc(value.size()+1));
        if (!result) throw std::bad_alloc();
        std::memcpy(result,value.c_str(),value.size()+1);
        return result;
    } catch (const std::exception& e) { uniform_error = e.what(); return nullptr; }
}
}
