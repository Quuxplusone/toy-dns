
#include "bytes.h"
#include "exception.h"
#include "ipaddressv4.h"
#include "rr.h"
#include "rrtype.h"
#include "symboltable.h"

#include <assert.h>
#include <regex>
#include <stdlib.h>
#include <string>
#include <string.h>

using namespace dns;

static std::string encode_rdata_repr_just_domain_name(const SymbolTable& syms, const std::string& rdata);

template<int rrtype>
static std::string decode_rdata_repr_just_domain_name(const char *src, const char *end);

struct by_rrtype_t {
    RRType type;
    const char *str;
    std::string (*encode_rdata_repr)(const SymbolTable& syms, const std::string& rdata);
    std::string (*decode_rdata_repr)(const char *src, const char *end);
};

static by_rrtype_t by_rrtype[] = {
    {
        RRType::A, "A",
        [](const SymbolTable&, const std::string& rdata) -> std::string {
            const char *src = rdata.data();
            const char *end = rdata.data() + rdata.size();
            IPAddressV4 ip;
            src = ip.decode(src, end);
            assert(src == end);
            return ip.repr();
        },
        [](const char *src, const char *end) -> std::string {
            IPAddressV4 ip;
            try {
                src = ip.decode_repr(src, end);
                if (src != end) throw dns::Exception("");
            } catch (const dns::Exception&) {
                throw dns::UnsupportedException("Zonefile RR of type A has a malformed IP address");
            }
            std::string result(4, '\0');
            char *dst = &result[0];
            dst = ip.encode(dst, result.data() + 4);
            assert(dst == result.data() + 4);
            return result;
        },
    },
    {
        RRType::NS, "NS",
        encode_rdata_repr_just_domain_name,
        decode_rdata_repr_just_domain_name<RRType::NS>,
    },
    {
        RRType::CNAME, "CNAME",
        encode_rdata_repr_just_domain_name,
        decode_rdata_repr_just_domain_name<RRType::CNAME>,
    },
    {
        RRType::SOA, "SOA",
        [](const SymbolTable& syms, const std::string& rdata) -> std::string {
            const char *src = rdata.data();
            const char *end = rdata.data() + rdata.size();
            Name primary_master_name;
            Name responsible_person_name;
            uint32_t serial_number;
            uint32_t refresh;
            uint32_t retry;
            uint32_t expire;
            uint32_t negative_caching_ttl;
            src = primary_master_name.decode(syms, src, end);
            src = responsible_person_name.decode(syms, src, end);
            src = get32bits(src, end, serial_number);
            src = get32bits(src, end, refresh);
            src = get32bits(src, end, retry);
            src = get32bits(src, end, expire);
            src = get32bits(src, end, negative_caching_ttl);
            assert(src == end);
            return (
                primary_master_name.repr() + " " +
                responsible_person_name.repr() + " " +
                std::to_string(serial_number) + " " +
                std::to_string(refresh) + " " +
                std::to_string(retry) + " " +
                std::to_string(expire) + " " +
                std::to_string(negative_caching_ttl)
            );
        },
        [](const char *src, const char *end) -> std::string {
            Name primary_master_name;
            Name responsible_person_name;

            while (src != nullptr && src != end && isspace(*src)) ++src;
            src = primary_master_name.decode_repr(src, end);
            while (src != nullptr && src != end && isspace(*src)) ++src;
            src = responsible_person_name.decode_repr(src, end);
            if (src == nullptr || src == end) throw dns::UnsupportedException("Zonefile RR of type SOA has the wrong format");

            std::regex rx("\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)$");
            std::cmatch m;
            bool success = std::regex_match(src, end, m, rx);
            if (!success) {
                throw dns::UnsupportedException("Zonefile RR of type SOA has the wrong format");
            }

            int serial_number = atoi(m[1].first);
            int refresh = atoi(m[2].first);
            int retry = atoi(m[3].first);
            int expire = atoi(m[4].first);
            int negative_caching_ttl = atoi(m[5].first);

            char buffer[255*64*2 + 20];
            char *dst = buffer;
            char *dst_end = buffer + sizeof buffer;
            dst = primary_master_name.encode(dst, dst_end);
            dst = responsible_person_name.encode(dst, dst_end);
            dst = put32bits(dst, dst_end, serial_number);
            dst = put32bits(dst, dst_end, refresh);
            dst = put32bits(dst, dst_end, retry);
            dst = put32bits(dst, dst_end, expire);
            dst = put32bits(dst, dst_end, negative_caching_ttl);
            assert(dst != nullptr);
            return std::string(buffer, dst);
        },
    },
    {
        RRType::PTR, "PTR",
        encode_rdata_repr_just_domain_name,
        decode_rdata_repr_just_domain_name<RRType::PTR>,
    },
    {
        RRType::MX, "MX",
        [](const SymbolTable& syms, const std::string& rdata) -> std::string {
            const char *src = rdata.data();
            const char *end = rdata.data() + rdata.size();
            uint16_t preference;
            Name exchange_name;
            src = get16bits(src, end, preference);
            src = exchange_name.decode(syms, src, end);
            assert(src == end);
            return std::to_string(preference) + " " + exchange_name.repr();
        },
        [](const char *src, const char *end) -> std::string {
            std::regex rx("(\\d+)\\s+(.*)$");
            std::cmatch m;
            bool success = std::regex_match(src, end, m, rx);
            if (!success) {
                throw dns::UnsupportedException("Zonefile RR of type MX has the wrong format");
            }
            int preference = atoi(m[1].first);
            if (preference < 0 || 65535 < preference) throw dns::UnsupportedException("Zonefile RR of type MX has an out-of-range PREFERENCE");
            Name exchange_name;
            src = exchange_name.decode_repr(m[2].first, m[2].second);
            if (src != end) throw dns::UnsupportedException("Zonefile RR of type MX has the wrong format");
            char buffer[255*64 + 2];
            char *p = put16bits(buffer, buffer + sizeof buffer, preference);
            char *buffer_end = exchange_name.encode(p, buffer + sizeof buffer);
            assert(buffer_end != nullptr);
            return std::string(buffer, buffer_end);
        },
    },
};

static std::string encode_rdata_repr_just_domain_name(const SymbolTable& syms, const std::string& rdata)
{
    Name canonical_name;
    const char *src = rdata.data();
    const char *end = src + rdata.size();
    src = canonical_name.decode(syms, src, end);
    assert(src == end);
    return canonical_name.repr();
}

template<int rrtype>
static std::string decode_rdata_repr_just_domain_name(const char *src, const char *end)
{
    Name canonical_name;
    src = canonical_name.decode_repr(src, end);
    if (src != end) throw dns::UnsupportedException("Zonefile RR of type ", RRType(rrtype).repr(), " has the wrong format");
    char buffer[255*64];
    char *buffer_end = canonical_name.encode(buffer, buffer + sizeof buffer);
    assert(buffer_end != nullptr);
    return std::string(buffer, buffer_end);
}

static std::string encode_unknown_rdata_repr(const std::string& rdata)
{
    // RFC 3597 "Handling of Unknown DNS Resource Record (RR) Types", section 5
    std::string result = "\\# ";
    result += std::to_string(rdata.size());
    result += ' ';
    for (uint8_t ch : rdata) {
        char buffer[10];
        snprintf(buffer, sizeof buffer, "%02x", unsigned(ch));
        result += buffer;
    }
    return result;
}

static std::string decode_unknown_rdata_repr(const char *src, const char *end)
{
    // RFC 3597 "Handling of Unknown DNS Resource Record (RR) Types", section 5
    auto to_hex = [](char c) -> int {
        if ('0' <= c && c <= '9') return (c - '0');
        if ('a' <= c && c <= 'f') return (c - 'a') + 10;
        if ('A' <= c && c <= 'F') return (c - 'A') + 10;
        return -1;
    };
    std::cmatch m;
    if (!std::regex_match(src, end, m, std::regex("\\\\#\\s+(\\d+)((\\s+[0-9a-fA-F ]*)?)$"))) {
        throw dns::UnsupportedException("Zonefile RR in RFC 3597 format has the wrong format");
    }
    int rdata_length = atoi(m[1].first);
    assert(m[2].second == end);
    std::string result;
    result.reserve(rdata_length);
    bool highorder = true;
    uint8_t in_progress = 0x00;
    for (src = m[2].first; src != end; ++src) {
        int digit = to_hex(*src);
        if (digit == -1) continue;
        if (highorder) {
            in_progress = (digit << 4);
        } else {
            result.push_back(uint8_t(in_progress | digit));
        }
        highorder = !highorder;
    }
    if (!highorder) {
        throw dns::UnsupportedException("Zonefile RR in RFC 3597 format has an odd number of hex digits");
    }
    if (result.size() != rdata_length) {
        throw dns::UnsupportedException("Zonefile RR in RFC 3597 format has the wrong length");
    }
    return result;
}

Name RR::rhs_name(const SymbolTable& syms) const
{
    assert(m_rrtype == RRType::NS || m_rrtype == RRType::CNAME);
    const char *src = m_rdata.data();
    const char *end = src + m_rdata.size();
    Name result;
    src = result.decode(syms, src, end);
    assert(src == end);
    return result;
}

const char *RR::decode(const SymbolTable& syms, const char *src, const char *end)
{
    src = m_name.decode(syms, src, end);
    src = get16bits(src, end, m_rrtype);
    src = get16bits(src, end, m_rrclass);
    src = get32bits(src, end, m_ttl);
    src = get_uint16_sized_string(src, end, m_rdata);
    return src;
}

char *RR::encode(char *dst, const char *end) const noexcept
{
    dst = m_name.encode(dst, end);
    dst = put16bits(dst, end, m_rrtype);
    dst = put16bits(dst, end, m_rrclass);
    dst = put32bits(dst, end, m_ttl);
    dst = put_uint16_sized_string(dst, end, m_rdata);
    return dst;
}

const char *RR::decode_repr(const char *src, const char *end)
{
    if (src == nullptr) return nullptr;
    while (src != end && isspace(*src)) ++src;
    src = m_name.decode_repr(src, end);
    if (src == nullptr) return nullptr;
    std::regex rx("\\s*(\\d+)\\s+(\\S+)\\s+(\\S+)\\s+(.*?)\\s*$");
    std::cmatch m;
    if (!std::regex_match(src, end, m, rx)) {
        throw dns::UnsupportedException("Zonefile RR has the wrong format");
    }
    int ttl = atoi(m[1].first);
    if (1 <= ttl && ttl <= 999999999) {
        m_ttl = ttl;
    } else {
        throw dns::UnsupportedException("Zonefile RR has an out-of-range TTL");
    }
    try {
        m_rrclass = int(RRClass(m.str(2)));
        if (m_rrclass != RRClass::IN) {
            throw dns::UnsupportedException("Zonefile RR has a class other than IN");
        }
    } catch (const dns::UnsupportedException&) {
        throw dns::UnsupportedException("Zonefile RR has unknown class ", m.str(2));
    }
    try {
        m_rrtype = int(RRType(m.str(3)));
        if (m_rrtype == RRType::ANY) {
            throw dns::UnsupportedException("Zonefile RR has type ANY");
        }
    } catch (const dns::UnsupportedException&) {
        throw dns::UnsupportedException("Zonefile RR has unknown type ", m.str(3));
    }
    // See whether the rdata matches the "RR of unknown type" text format.
    if (m[4].length() >= 2 && m[4].first[0] == '\\' && m[4].first[1] == '#') {
        m_rdata = decode_unknown_rdata_repr(m[4].first, m[4].second);
    } else {
        for (auto&& rrt : by_rrtype) {
            // See whether we know a custom RDATA-parsing routine for this specific rrtype.
            if (RRType(m_rrtype) == rrt.type) {
                assert(rrt.decode_rdata_repr != nullptr);
                m_rdata = rrt.decode_rdata_repr(m[4].first, m[4].second);
                break;
            }
        }
    }
    return end;
}

std::string RR::repr(const SymbolTable& syms) const
{
    std::string result;
    result += m_name.repr();
    do { result += ' '; } while (result.size() < 32);
    do { result += ' '; } while ((result.size() % 8) != 0);
    result += std::to_string(m_ttl);
    do { result += ' '; } while ((result.size() % 8) != 0);
    assert(m_rrclass == RRClass::IN);
    result += "IN";
    result += ' ';
    result += RRType(m_rrtype).repr();
    do { result += ' '; } while ((result.size() % 8) != 0);
    bool success = false;
    for (auto&& rrt : by_rrtype) {
        if (int(rrt.type) == m_rrtype) {
            assert(rrt.encode_rdata_repr != nullptr);
            result += rrt.encode_rdata_repr(syms, m_rdata);
            success = true;
            break;
        }
    }
    if (!success) {
        result += encode_unknown_rdata_repr(m_rdata);
    }
    return result;
}
