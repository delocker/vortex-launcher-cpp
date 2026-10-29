// json.hpp - minimal JSON parser replacing PureBasic's ParseJSON/GetJSONMember API.
// Based on Vortex Minecraft Launcher by Kron4ek (PureBasic, GPL-3.0):
// https://github.com/Kron4ek/minecraft-vortex-launcher - see LICENSE at the repo root.
// Preserves object member order (like PB's ExamineJSONMembers iteration).
#pragma once
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cctype>

struct J {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;

    // PureBasic GetJSONMember()
    const J* m(const std::string& key) const {
        for (const auto& kv : o) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    // GetJSONString(): "" when missing/not a string
    std::string ms(const std::string& key) const {
        const J* x = m(key);
        return (x && x->t == STR) ? x->s : std::string();
    }
    std::string str() const { return t == STR ? s : std::string(); }
    // GetJSONInteger(): 0 when missing/not a number
    long long mi(const std::string& key, long long def = 0) const {
        const J* x = m(key);
        return (x && x->t == NUM) ? (long long)x->n : def;
    }
};

namespace jsonp {
struct P {
    const std::string& src;
    size_t i = 0;
    bool ok = true;

    void ws() { while (i < src.size() && (src[i]==' '||src[i]=='\t'||src[i]=='\n'||src[i]=='\r')) ++i; }
    bool lit(const char* l) {
        size_t L = strlen(l);
        if (src.compare(i, L, l) == 0) { i += L; return true; }
        ok = false; return false;
    }
    void utf8(unsigned cp, std::string& out) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0|(cp>>6)); out += (char)(0x80|(cp&0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
        else { out += (char)(0xF0|(cp>>18)); out += (char)(0x80|((cp>>12)&0x3F)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
    }
    unsigned hex4() {
        unsigned v = 0;
        for (int k = 0; k < 4 && i < src.size(); ++k) {
            char c = src[i++];
            if (c >= '0' && c <= '9') v = v * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') v = v * 16 + (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v = v * 16 + (c - 'A' + 10);
            else ok = false;
        }
        return v;
    }
    std::string str() {
        std::string out;
        if (i >= src.size() || src[i] != '"') { ok = false; return out; }
        ++i;
        while (i < src.size() && src[i] != '"') {
            char c = src[i++];
            if (c == '\\') {
                if (i >= src.size()) { ok = false; break; }
                char e = src[i++];
                switch (e) {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u': {
                        unsigned cp = hex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < src.size() && src[i] == '\\' && src[i+1] == 'u') {
                            i += 2;
                            unsigned lo = hex4();
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        utf8(cp, out);
                        break;
                    }
                    default: ok = false;
                }
            } else out += c;
        }
        if (i < src.size() && src[i] == '"') ++i; else ok = false;
        return out;
    }
    void value(J& v) {
        ws();
        if (i >= src.size()) { ok = false; return; }
        char c = src[i];
        if (c == '{') {
            v.t = J::OBJ; ++i; ws();
            if (i < src.size() && src[i] == '}') { ++i; return; }
            while (ok) {
                ws();
                std::string k = str();
                ws();
                if (i >= src.size() || src[i] != ':') { ok = false; break; }
                ++i;
                J item; value(item);
                v.o.emplace_back(std::move(k), std::move(item));
                ws();
                if (i < src.size() && src[i] == ',') { ++i; continue; }
                if (i < src.size() && src[i] == '}') { ++i; break; }
                ok = false;
            }
            return;
        }
        if (c == '[') {
            v.t = J::ARR; ++i; ws();
            if (i < src.size() && src[i] == ']') { ++i; return; }
            while (ok) {
                J item; value(item);
                v.a.push_back(std::move(item));
                ws();
                if (i < src.size() && src[i] == ',') { ++i; continue; }
                if (i < src.size() && src[i] == ']') { ++i; break; }
                ok = false;
            }
            return;
        }
        if (c == '"') { v.t = J::STR; v.s = str(); return; }
        if (c == 't') { if (lit("true")) { v.t = J::BOOL; v.b = true; } return; }
        if (c == 'f') { if (lit("false")) { v.t = J::BOOL; v.b = false; } return; }
        if (c == 'n') { lit("null"); return; }
        size_t st = i;
        while (i < src.size() && (isdigit((unsigned char)src[i]) || src[i]=='-' || src[i]=='+' || src[i]=='.' || src[i]=='e' || src[i]=='E')) ++i;
        if (st == i) { ok = false; return; }
        v.t = J::NUM;
        v.n = strtod(src.substr(st, i - st).c_str(), nullptr);
    }
};
inline bool parse(const std::string& src, J& root) {
    if (src.empty()) return false;
    P p{ src };
    p.value(root);
    return p.ok && root.t != J::NUL;
}
} // namespace jsonp
