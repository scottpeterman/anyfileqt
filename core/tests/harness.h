// core/tests/harness.h
//
// Enough of a test framework to not need one -- anyeditqt's harness, with
// QString printing added. Deliberately not QtTest, Catch2 or GoogleTest: one
// header, no dependency, and a failure names the file and line.
#pragma once

#include <QString>
#include <QStringList>

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace harness {

struct Case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case> &registry() {
    static std::vector<Case> r;
    return r;
}

inline int &failures() {
    static int f = 0;
    return f;
}

struct Register {
    Register(const std::string &name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

inline void fail(const char *file, int line, const std::string &msg) {
    ++failures();
    std::fprintf(stderr, "  FAIL %s:%d  %s\n", file, line, msg.c_str());
}

template <class T>
void print(std::ostream &os, const T &v) {
    os << v;
}
inline void print(std::ostream &os, const QString &v) {
    os << '"' << v.toStdString() << '"';
}
inline void print(std::ostream &os, const QStringList &v) {
    os << '[' << v.join(QStringLiteral(", ")).toStdString() << ']';
}
inline void print(std::ostream &os, const QByteArray &v) {
    os << '"' << v.toStdString() << '"';
}

template <class A, class B>
void checkEq(const char *file, int line, const A &a, const B &b, const char *expr) {
    if (a == b) return;
    std::ostringstream ss;
    ss << expr << "\n        got      ";
    print(ss, a);
    ss << "\n        expected ";
    print(ss, b);
    fail(file, line, ss.str());
}

inline void checkTrue(const char *file, int line, bool v, const char *expr) {
    if (!v) fail(file, line, std::string("expected true: ") + expr);
}

inline int run(const char *only = nullptr) {
    int cases = 0;
    for (auto &c : registry()) {
        if (only && c.name.find(only) == std::string::npos) continue;
        const int before = failures();
        c.fn();
        ++cases;
        if (failures() > before) std::fprintf(stderr, "  in case: %s\n", c.name.c_str());
    }
    std::fprintf(stderr, "%d cases, %d failures\n", cases, failures());
    return failures() == 0 ? 0 : 1;
}

}  // namespace harness

#define TEST(name)                                    \
    static void name();                               \
    static harness::Register reg_##name(#name, name); \
    static void name()

#define CHECK_EQ(a, b) harness::checkEq(__FILE__, __LINE__, (a), (b), #a " == " #b)
#define CHECK(x) harness::checkTrue(__FILE__, __LINE__, (x), #x)
