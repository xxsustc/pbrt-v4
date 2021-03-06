// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef PBRT_UTIL_ARGS_H
#define PBRT_UTIL_ARGS_H

#include <pbrt/pbrt.h>
#include <pbrt/util/print.h>
#include <pbrt/util/pstd.h>
#include <pbrt/util/string.h>

#include <cctype>
#include <cstring>
#include <functional>
#include <string>

namespace pbrt {
namespace {

// Downcase the string and remove any '-' or '_' characters; thus we can be
// a little flexible in what we match for argument names.
std::string normalizeArg(const std::string &str) {
    std::string ret;
    for (unsigned char c : str) {
        if (c != '_' && c != '-')
            ret += std::tolower(c);
    }
    return ret;
}

bool initArg(const std::string &str, int *ptr) {
    if (str.empty() || (!std::isdigit(str[0]) && str[0] != '-'))
        return false;
    *ptr = std::stoi(str);
    return true;
}

bool initArg(const std::string &str, float *ptr) {
    if (str.empty() || (!std::isdigit(str[0]) && str[0] != '-' && str[0] != '.'))
        return false;
    *ptr = std::stof(str);
    return true;
}

bool initArg(const std::string &str, double *ptr) {
    if (str.empty() || (!std::isdigit(str[0]) && str[0] != '-' && str[0] != '.'))
        return false;
    *ptr = std::stod(str);
    return true;
}

bool initArg(const std::string &str, pstd::span<float> out) {
    pstd::optional<std::vector<Float>> v = SplitStringToFloats(str, ',');
    if (!v || v->size() != out.size())
        return false;
    std::copy(v->begin(), v->end(), out.begin());
    return true;
}

bool initArg(const std::string &str, pstd::span<double> out) {
    pstd::optional<std::vector<double>> v = SplitStringToDoubles(str, ',');
    if (!v || v->size() != out.size())
        return false;
    std::copy(v->begin(), v->end(), out.begin());
    return true;
}

bool initArg(const std::string &str, pstd::span<int> out) {
    pstd::optional<std::vector<int>> v = SplitStringToInts(str, ',');
    if (!v || v->size() != out.size())
        return false;
    std::copy(v->begin(), v->end(), out.begin());
    return true;
}

bool initArg(const std::string &str, char **ptr) {
    if (str.empty())
        return false;
    *ptr = new char[str.size() + 1];
    std::strcpy(*ptr, str.c_str());
    return true;
}

bool initArg(const std::string &str, std::string *ptr) {
    if (str.empty())
        return false;
    *ptr = str;
    return true;
}

bool initArg(const std::string &str, bool *ptr) {
    if (normalizeArg(str) == "false") {
        *ptr = false;
        return true;
    } else if (normalizeArg(str) == "true") {
        *ptr = true;
        return true;
    }
    return false;
}

template <typename T>
bool initArg(const std::string &str, pstd::optional<T> *ptr) {
    T value;
    if (initArg(str, &value)) {
        *ptr = value;
        return true;
    }
    return false;
}

bool matchPrefix(const std::string &str, const std::string &prefix) {
    if (prefix.size() > str.size())
        return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (prefix[i] != str[i])
            return false;
    return true;
}

template <typename T>
bool enable(T ptr) {
    return false;
}

bool enable(bool *ptr) {
    *ptr = true;
    return true;
}

}  // namespace

// T basically needs to be a pointer type or a Span.
template <typename T>
bool ParseArg(char ***argv, const std::string &name, T out,
              std::function<void(std::string)> onError) {
    std::string arg = **argv;

    // Strip either one or two leading dashes.
    if (arg[1] == '-')
        arg = arg.substr(2);
    else
        arg = arg.substr(1);

    if (matchPrefix(normalizeArg(arg), normalizeArg(name + '='))) {
        // --arg=value
        *argv += 1;
        std::string value = arg.substr(name.size() + 1);
        if (!initArg(value, out)) {
            onError(StringPrintf("invalid value \"%s\" for %s argument", value, name));
            return false;
        }
        return true;
    } else if (normalizeArg(arg) == normalizeArg(name)) {
        // --arg <value>, except for bool arguments, which are set to true
        // without expecting another argument.
        *argv += 1;
        if (enable(out))
            return true;

        if (**argv == nullptr) {
            onError(StringPrintf("missing value after %s argument", arg));
            return false;
        }
        initArg(**argv, out);
        *argv += 1;
        return true;
    } else
        return false;
}

}  // namespace pbrt

#endif  // PBRT_UTIL_ARGS_H
