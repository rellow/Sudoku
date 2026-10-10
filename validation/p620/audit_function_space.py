#!/usr/bin/env python3

masks = [
    0x088890000,
    0x111120000,
    0x444408000,
    0x222278000,
    0x00007fec0,
    0x000804100,
]

def rank(vectors):
    basis = {}

    for value in vectors:
        x = value

        while x:
            pivot = x.bit_length() - 1

            if pivot not in basis:
                basis[pivot] = x
                break

            x ^= basis[pivot]

    return len(basis)

def equivalent(a, b):
    return rank(a) == rank(b) == rank(a + b)

alternative = [
    masks[0] ^ masks[1],
    masks[1],
    masks[2],
    masks[3],
    masks[4],
    masks[5],
]

print("Functions:", len(masks))
print("GF(2) rank:", rank(masks))
print("Independent:", rank(masks) == len(masks))
print("Equivalent alternative basis:", equivalent(masks, alternative))
