// MIT License
// 
// Copyright (c) 2021 Proton
// Copyright (c) 2019 worldwide-asset-exchange
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <rng/rng.hpp>
#include <boost/multiprecision/cpp_int.hpp>

using BN = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>>;

namespace proton {

ACTION rng::pause(
    bool paused
) {
    require_auth(get_self());
    set_config(paused_row, uint64_t(paused));
}

ACTION rng::requestrand(
    uint64_t assoc_id, 
    uint64_t signing_value, 
    const name& caller
) {
    require_auth(caller);
    check(!is_paused(), "Contract is paused");

    auto it = signvals_table.find(signing_value);
    check(it == signvals_table.end(), "Signing value already used");

    signvals_table.emplace(caller, [&](auto& rec) {
        rec.signing_value = signing_value;
    });

    jobs_table.emplace(caller, [&](auto& rec) {
        rec.id = generate_next_index();
        rec.assoc_id = assoc_id;
        rec.signing_value = signing_value;
        rec.caller = caller;
    });
}

ACTION rng::setrand(
    uint64_t job_id,
    const string& random_value
) {
    require_auth(get_self());
    check(!is_paused(), "Contract is paused");

    auto job_it = jobs_table.find(job_id);
    check(job_it != jobs_table.end(), "Could not find job id.");

    auto sig_it = sigpubkey_table.find(0);
    
    check(sig_it != sigpubkey_table.end(), "Could not find a value in sigpubkey table.");
    verify_rsa_sha256_sig(to_string(job_it->signing_value), random_value, sig_it->exponent, sig_it->modulus);
    
    checksum256 rv_hash = sha256(random_value.data(), random_value.size());

    action(
        {get_self(), "active"_n}, 
        job_it->caller, "receiverand"_n,
        std::tuple(job_it->assoc_id, rv_hash)
    ).send();

    jobs_table.erase(job_it);
}

ACTION rng::killjobs(
    const std::vector<uint64_t>& job_ids
) {
    require_auth(get_self());

    for (const auto& id : job_ids) {
        auto job_it = jobs_table.find(id);
        if (job_it != jobs_table.end()) {
            jobs_table.erase(job_it);
        }
    }
}

ACTION rng::setsigpubkey(
    const std::string& exponent, 
    const std::string& modulus
) {
    require_auth(get_self());

    check(modulus.size() > 0, "modulus must have non-zero length");
    check(modulus[0] != '0', "modulus must have leading zeroes stripped");

    auto it = sigpubkey_table.find(0);
    if (it == sigpubkey_table.end()) {
        sigpubkey_table.emplace(get_self(), [&](auto& rec) {
            rec.id = 0;
            rec.exponent = exponent;
            rec.modulus = modulus;
        });
    }
    else {
        sigpubkey_table.modify(it, same_payer, [&](auto& rec) {
            rec.exponent = exponent;
            rec.modulus = modulus;
        });
    }
}

bool rng::is_paused() const {
    return get_config(paused_row, false);
}

void rng::set_config(
    uint64_t name, int64_t value
) {
    auto it = config_table.find(name);
    if (it == config_table.end()) {
        config_table.emplace(get_self(), [&](auto& rec) {
            rec.name = name;
            rec.value = value;
        });
    } 
    else {
        config_table.modify(it, get_self(), [&](auto& rec) {
            rec.value = value;
        });
    }
}

int64_t rng::get_config(
    uint64_t name,
    int64_t default_value
) const {
    auto it = config_table.find(name);
    if (it == config_table.end()) 
        return default_value;
    return it->value;
}

uint64_t rng::generate_next_index() {
    const uint64_t entry_name{"jobid.index"_n.value};
    int64_t index_val = get_config(entry_name, 0);
    set_config(entry_name, index_val + 1);
    return index_val;
}

} // end namespace proton