// Independent RUP-only LRAT checker: rejects RAT hints instead of trusting them.
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("check_lrat cnf proof.lrat");
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("Cannot open CNF");
        std::vector<std::vector<int>> clauses(1);
        std::vector<bool> active(1, false);
        std::string line;
        int nv = 0, declared = -1;
        std::vector<int> pending;
        bool empty = false;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == 'c') continue;
            std::istringstream stream(line);
            if (line[0] == 'p') {
                std::string p, kind;
                stream >> p >> kind >> nv >> declared;
                if (kind != "cnf" || nv < 0 || declared < 0) throw std::runtime_error("Invalid CNF header");
                continue;
            }
            int lit;
            while (stream >> lit) {
                if (lit) {
                    if (lit == INT32_MIN || abs(lit) > nv) throw std::runtime_error("CNF literal out of range");
                    pending.push_back(lit);
                } else {
                    empty |= pending.empty();
                    clauses.push_back(std::move(pending));
                    pending.clear();
                    active.push_back(true);
                }
            }
        }
        if (!pending.empty() || declared != int(clauses.size())-1) throw std::runtime_error("Wrong CNF clause count");
        std::ifstream proof(argv[2]);
        if (!proof) throw std::runtime_error("Cannot open proof");
        std::vector<int8_t> assignment(nv+1, 0);
        std::vector<int> touched;
        uint64_t additions = 0, last = clauses.size()-1, lineno = 0;
        while (std::getline(proof, line)) {
            ++lineno;
            if (line.empty() || line[0] == 'c') continue;
            std::istringstream stream(line);
            uint64_t id;
            std::string second;
            if (!(stream >> id >> second)) throw std::runtime_error("Invalid LRAT line");
            if (second == "d") {
                uint64_t remove;
                bool terminated = false;
                while (stream >> remove) {
                    if (!remove) { terminated = true; break; }
                    if (remove >= active.size() || !active[remove]) throw std::runtime_error("Invalid deletion");
                    active[remove] = false;
                    std::vector<int>().swap(clauses[remove]);
                }
                if (!terminated) throw std::runtime_error("Unterminated deletion");
                continue;
            }
            if (id <= last) throw std::runtime_error("Nonincreasing clause ID");
            last = id;
            int lit = std::stoi(second);
            std::vector<int> clause;
            while (lit) {
                if (lit == INT32_MIN) throw std::runtime_error("Invalid literal");
                clause.push_back(lit);
                if (!(stream >> lit)) throw std::runtime_error("Unterminated clause");
            }
            bool conflict = false;
            auto assign = [&](int x) {
                size_t v = abs(x);
                if (v >= assignment.size()) assignment.resize(v+1, 0);
                int8_t value = x > 0 ? 1 : -1;
                if (assignment[v] && assignment[v] != value) conflict = true;
                else if (!assignment[v]) { assignment[v] = value; touched.push_back(v); }
            };
            for (int x : clause) assign(-x);
            int64_t hint;
            bool terminated = false;
            while (stream >> hint) {
                if (!hint) { terminated = true; break; }
                if (hint < 0) throw std::runtime_error("RAT hints unsupported");
                if (uint64_t(hint) >= clauses.size() || !active[hint]) throw std::runtime_error("Invalid antecedent");
                if (conflict) continue;
                int unit = 0;
                bool satisfied = false, nonunit = false;
                for (int x : clauses[hint]) {
                    if (size_t(abs(x)) >= assignment.size()) assignment.resize(abs(x)+1, 0);
                    int value = assignment[abs(x)] * (x > 0 ? 1 : -1);
                    if (value > 0) { satisfied = true; break; }
                    if (!value) {
                        if (unit && unit != x) nonunit = true;
                        unit = x;
                    }
                }
                // A hint can repeat a unit already implied by the assumptions.
                // It adds no information; every actual propagation still needs
                // exactly one unassigned literal, and acceptance needs conflict.
                if (satisfied) continue;
                if (nonunit) throw std::runtime_error("Nonunit antecedent at proof line " + std::to_string(lineno));
                if (unit) assign(unit);
                else conflict = true;
            }
            if (!terminated || !conflict) throw std::runtime_error("Failed RUP on line " + std::to_string(lineno));
            for (int v : touched) assignment[v] = 0;
            touched.clear();
            clauses.resize(id+1);
            active.resize(id+1, false);
            empty |= clause.empty();
            clauses[id] = std::move(clause);
            active[id] = true;
            ++additions;
        }
        if (!empty) throw std::runtime_error("No empty clause proved");
        std::cout << "{\"verified\":true,\"initial_clauses\":" << declared << ",\"additions\":" << additions << "}\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
