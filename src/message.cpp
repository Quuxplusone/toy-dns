
#include "bytes.h"
#include "message.h"
#include "opcode.h"
#include "rcode.h"

#include <stdlib.h>
#include <string>
#include <utility>

using namespace dns;

Message Message::beginQuery(Question question) noexcept
{
    Message query;
    query.setID(rand());
    query.setOpcode(Opcode::QUERY);
    query.setQR(false);
    query.add_question(std::move(question));
    return query;
}

Message Message::beginResponseTo(const Message& query) noexcept
{
    Message response;
    response.m_id = query.m_id;
    response.m_qr = true;
    response.m_opcode = query.m_opcode;
    response.m_rd = query.m_rd;
    return response;
}

const char *Message::decode(const char *packet_start, const char *end)
{
    const char *src = packet_start;

    uint16_t fields;
    uint16_t qdcount, ancount, nscount, arcount;

    src = get16bits(src, end, m_id);
    src = get16bits(src, end, fields);
    src = get16bits(src, end, qdcount);
    src = get16bits(src, end, ancount);
    src = get16bits(src, end, nscount);
    src = get16bits(src, end, arcount);

    if (src == nullptr) return nullptr;

    m_qr = ((fields >> 15) & 0x1);
    m_opcode = static_cast<Opcode>((fields >> 11) & 0xF);
    m_aa = ((fields >> 10) & 0x1);
    m_tc = ((fields >> 9) & 0x1);
    m_rd = ((fields >> 8) & 0x1);
    m_ra = ((fields >> 7) & 0x1);
    m_rcode = static_cast<RCode>((fields >> 0) & 0xF);

    // Now, before parsing any other names, build a "symbol table"
    // consisting of all the names that could possibly be encoded
    // in this packet's bytes.
    m_symbol_table.build(packet_start, end);

    for (uint16_t i=0; i < qdcount; ++i) {
        m_question.emplace_back();
        src = m_question.back().decode(m_symbol_table, src, end);
        if (src == nullptr) return nullptr;
    }
    for (uint16_t i=0; i < ancount; ++i) {
        m_answer.emplace_back();
        src = m_answer.back().decode(m_symbol_table, src, end);
        if (src == nullptr) return nullptr;
    }
    for (uint16_t i=0; i < nscount; ++i) {
        m_authority.emplace_back();
        src = m_authority.back().decode(m_symbol_table, src, end);
        if (src == nullptr) return nullptr;
    }
    for (uint16_t i=0; i < arcount; ++i) {
        m_additional.emplace_back();
        src = m_additional.back().decode(m_symbol_table, src, end);
        if (src == nullptr) return nullptr;
    }
    return src;
}

char *Message::encode(char *dst, const char *end) const noexcept
{
    int fields = (int(m_qr) << 15);
    fields |= (int(m_opcode) << 11);
    fields |= (int(m_aa) << 10);
    fields |= (int(m_tc) << 9);
    fields |= (int(m_rd) << 8);
    fields |= (int(m_ra) << 7);
    fields |= (int(m_rcode) << 0);

    dst = put16bits(dst, end, m_id);
    dst = put16bits(dst, end, fields);
    dst = put16bits(dst, end, m_question.size());
    dst = put16bits(dst, end, m_answer.size());
    dst = put16bits(dst, end, m_authority.size());
    dst = put16bits(dst, end, m_additional.size());

    for (auto&& q : m_question) {
        dst = q.encode(dst, end);
    }
    for (auto&& rr : m_answer) {
        dst = rr.encode(dst, end);
    }
    for (auto&& rr : m_authority) {
        dst = rr.encode(dst, end);
    }
    for (auto&& rr : m_additional) {
        dst = rr.encode(dst, end);
    }
    return dst;
}

std::string Message::repr() const
{
    std::string result;

    result += ";; ->>HEADER<<- opcode: ";
    result += Opcode(m_opcode).repr();
    result += ", status: ";
    result += RCode(m_rcode).repr();
    result += ", id: ";
    result += std::to_string(m_id);
    result += "\n";

    result += ";; flags: ";
    if (m_qr) result += " qr";
    if (m_aa) result += " aa";
    if (m_tc) result += " tc";
    if (m_rd) result += " rd";
    if (m_ra) result += " ra";
    result += "; QUERY: " + std::to_string(m_question.size());
    result += ", ANSWER: " + std::to_string(m_answer.size());
    result += ", AUTHORITY: " + std::to_string(m_authority.size());
    result += ", ADDITIONAL: " + std::to_string(m_additional.size());
    result += "\n";

    if (m_rd && !m_ra) {
        result += ";; WARNING: recursion requested but not available\n";
    }

    if (!m_question.empty()) {
        result += "\n;; QUESTION SECTION:\n";
        for (auto&& q : m_question) {
            result += ";" + q.repr() + "\n";
        }
    }

    if (!m_answer.empty()) {
        result += "\n;; ANSWER SECTION:\n";
        for (auto&& rr : m_answer) {
            result += rr.repr(m_symbol_table) + "\n";
        }
    }

    if (!m_authority.empty()) {
        result += "\n;; AUTHORITY SECTION:\n";
        for (auto&& rr : m_authority) {
            result += rr.repr(m_symbol_table) + "\n";
        }
    }

    if (!m_additional.empty()) {
        result += "\n;; AUTHORITY SECTION:\n";
        for (auto&& rr : m_additional) {
            result += rr.repr(m_symbol_table) + "\n";
        }
    }

    return result;
}
