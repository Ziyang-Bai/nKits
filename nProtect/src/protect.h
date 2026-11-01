#ifndef NPROTECT_PROTECT_H
#define NPROTECT_PROTECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROTECT_MAX_KEYS 4
#define PROTECT_CONFIG_SIZE 128
#define PROTECT_KEYS(APPLY) \
    APPLY(EQU, "=") APPLY(CTRL, "CTRL") APPLY(SHIFT, "SHIFT") \
    APPLY(A, "A") APPLY(B, "B") APPLY(C, "C") APPLY(D, "D") APPLY(E, "E") \
    APPLY(F, "F") APPLY(G, "G") APPLY(H, "H") APPLY(I, "I") APPLY(J, "J") \
    APPLY(K, "K") APPLY(L, "L") APPLY(M, "M") APPLY(N, "N") APPLY(O, "O") \
    APPLY(P, "P") APPLY(Q, "Q") APPLY(R, "R") APPLY(S, "S") APPLY(T, "T") \
    APPLY(U, "U") APPLY(V, "V") APPLY(W, "W") APPLY(X, "X") APPLY(Y, "Y") APPLY(Z, "Z") \
    APPLY(0, "0") APPLY(1, "1") APPLY(2, "2") APPLY(3, "3") APPLY(4, "4") \
    APPLY(5, "5") APPLY(6, "6") APPLY(7, "7") APPLY(8, "8") APPLY(9, "9") \
    APPLY(PLUS, "PLUS") APPLY(MINUS, "MINUS") APPLY(MULTIPLY, "MULTIPLY") APPLY(DIVIDE, "DIVIDE") \
    APPLY(LP, "LP") APPLY(RP, "RP") APPLY(PERIOD, "PERIOD") APPLY(SPACE, "SPACE") \
    APPLY(VAR, "VAR") APPLY(TRIG, "TRIG") APPLY(PI, "PI") APPLY(EXP, "EXP") \
    APPLY(SQU, "SQU") APPLY(EE, "EE") APPLY(CAT, "CAT") APPLY(FRAC, "FRAC") \
    APPLY(NEGATIVE, "NEGATIVE") APPLY(DEL, "DEL") APPLY(TAB, "TAB")

typedef enum {
#define PROTECT_ENUM(key, name) PROTECT_KEY_##key,
    PROTECT_KEYS(PROTECT_ENUM)
#undef PROTECT_ENUM
    PROTECT_KEY_COUNT
} ProtectKey;

typedef struct {
    unsigned count;
    ProtectKey keys[PROTECT_MAX_KEYS];
} ProtectCombo;

typedef struct {
    bool enabled;
    ProtectCombo combo;
} Protect;

const char *protect_key_name(ProtectKey key);
void protect_defaults(Protect *protect);
bool protect_parse(ProtectCombo *combo, const char *text);
void protect_format(const ProtectCombo *combo, char text[PROTECT_CONFIG_SIZE]);
bool protect_decode(Protect *protect, const char *data, size_t size);
size_t protect_encode(const Protect *protect, char data[PROTECT_CONFIG_SIZE]);

#endif
