#ifndef TRACER_MACRO_UTILS_H
#define TRACER_MACRO_UTILS_H

// MAP_STRINGIFY(Label1, Label2, ..., LabelN)
//  => "Label1", "Label2", ..., "LabelN"
// Up to 16 arguments are supported.

// Basic stringification
#define _STRINGIFY(x) #x
#define STRINGIFY(x) _STRINGIFY(x)

// Map STRINGIFY over up to 16 arguments
#define MAP_STRINGIFY_1(a) \
    STRINGIFY(a)
#define MAP_STRINGIFY_2(a, b) \
    MAP_STRINGIFY_1(a), STRINGIFY(b)
#define MAP_STRINGIFY_3(a, b, c) \
    MAP_STRINGIFY_2(a, b), STRINGIFY(c)
#define MAP_STRINGIFY_4(a, b, c, d) \
    MAP_STRINGIFY_3(a, b, c), STRINGIFY(d)
#define MAP_STRINGIFY_5(a, b, c, d, e) \
    MAP_STRINGIFY_4(a, b, c, d), STRINGIFY(e)
#define MAP_STRINGIFY_6(a, b, c, d, e, f) \
    MAP_STRINGIFY_5(a, b, c, d, e), STRINGIFY(f)
#define MAP_STRINGIFY_7(a, b, c, d, e, f, g) \
    MAP_STRINGIFY_6(a, b, c, d, e, f), STRINGIFY(g)
#define MAP_STRINGIFY_8(a, b, c, d, e, f, g, h) \
    MAP_STRINGIFY_7(a, b, c, d, e, f, g), STRINGIFY(h)
#define MAP_STRINGIFY_9(a, b, c, d, e, f, g, h, i) \
    MAP_STRINGIFY_8(a, b, c, d, e, f, g, h), STRINGIFY(i)
#define MAP_STRINGIFY_10(a, b, c, d, e, f, g, h, i, j) \
    MAP_STRINGIFY_9(a, b, c, d, e, f, g, h, i), STRINGIFY(j)
#define MAP_STRINGIFY_11(a, b, c, d, e, f, g, h, i, j, k) \
    MAP_STRINGIFY_10(a, b, c, d, e, f, g, h, i, j), STRINGIFY(k)
#define MAP_STRINGIFY_12(a, b, c, d, e, f, g, h, i, j, k, l) \
    MAP_STRINGIFY_11(a, b, c, d, e, f, g, h, i, j, k), STRINGIFY(l)
#define MAP_STRINGIFY_13(a, b, c, d, e, f, g, h, i, j, k, l, m) \
    MAP_STRINGIFY_12(a, b, c, d, e, f, g, h, i, j, k, l), STRINGIFY(m)
#define MAP_STRINGIFY_14(a, b, c, d, e, f, g, h, i, j, k, l, m, n) \
    MAP_STRINGIFY_13(a, b, c, d, e, f, g, h, i, j, k, l, m), STRINGIFY(n)
#define MAP_STRINGIFY_15(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o) \
    MAP_STRINGIFY_14(a, b, c, d, e, f, g, h, i, j, k, l, m, n), STRINGIFY(o)
#define MAP_STRINGIFY_16(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p) \
    MAP_STRINGIFY_15(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o), STRINGIFY(p)
#define MAP_STRINGIFY_OVERFLOW(...) \
    static_assert(0, "Too many arguments to MAP_STRINGIFY — max is 16.")

// Argument count dispatcher
#define GET_MAP_STRINGIFY_MACRO(             \
    _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, \
    _11, _12, _13, _14, _15, _16, NAME, ...) NAME

#define MAP_STRINGIFY(...)                                                      \
    GET_MAP_STRINGIFY_MACRO(                                                    \
        __VA_ARGS__,                                                            \
        MAP_STRINGIFY_16, MAP_STRINGIFY_15, MAP_STRINGIFY_14, MAP_STRINGIFY_13, \
        MAP_STRINGIFY_12, MAP_STRINGIFY_11, MAP_STRINGIFY_10, MAP_STRINGIFY_9,  \
        MAP_STRINGIFY_8, MAP_STRINGIFY_7, MAP_STRINGIFY_6, MAP_STRINGIFY_5,     \
        MAP_STRINGIFY_4, MAP_STRINGIFY_3, MAP_STRINGIFY_2, MAP_STRINGIFY_1,     \
        MAP_STRINGIFY_OVERFLOW)(__VA_ARGS__)

#endif // TRACER_MACRO_UTILS_H
