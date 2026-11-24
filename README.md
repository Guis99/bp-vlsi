# Hardware simulation of a basic TAGE Branch Predictor
RTL simulation + transistor-level schematic + layout of a TAGE branch predictor

This project is a continuation of my exploration of branch predictors documented [here](https://bhhoh.com/blog/branch-prediction/). The goal is to better understand the details of computer hardware design by turning a toy C++ model into a slightly more realistic hardware model.

## Behavioral spec
The implementation of the software-defined TAGE predictor is outlined below. It is intended to inform the HDL and layout variants so it is quite terse and jargon-heavy. All three are expected to have logically identical outputs for a given input vector and predictor state. The spec is based largely on the original [Seznec paper](https://inria.hal.science/hal-03408381/document), but does not strictly adhere to it.

### Sizing

Since shrinking the TAGE Predictor will almost certainly degrade its performance on real traces, a Bimodal and GShare predictor will be used as a comparison point. All three predictors are sized to be no more than 1KiB (2^10 bytes). The TAGE predictor is sized as follows:

| Parameter | Value |
| :--- | :---: |
| Number of history lengths | 8 |
| History lengths | 4, 6, 11, 19, 33, 56, 96, 164 |
| Entries per provider bank | 64 |
| Tag width | 6 |
| Base table entries | 512 |

This results in a total size of .93 KiB, or 7604 bits. The Bimodal and Gshare predictors are sized to be 8192 and 8202 bits, respectively.

### Indexing
The base table is indexed by the lower 9 bits of the branch address. Equivalent to the operation 

```
index = pc & 0x1FF
```

Indices for each of the predictor banks will be computed the branch address and the corresponding history length using a folded XOR hash with an index width of 6. As an example using history length 19:

```
h = 0x6FA2D = 0b110 1111 1010 0010 1101
index = 000001 ^ 101111 ^ 101000 ^ 101101 ^ (pc & 0x3F)
```

### Tagging
The tags are computed similarly but with an additional 5 bit-wide folded history hash shifted to the left by 1:

```
h = 0x6FA2D = 0b110 1111 1010 0010 1101
tag_hash_1 = 000001 ^ 101111 ^ 101000 ^ 101101 
tag_hash_2 = 01101 ^ 11110 ^ 10001 ^ 01101
tag = tag_hash_1 ^ (tag_hash_2 << 1) ^ (pc & 0x3F)
```

### Primitive components
- Each entry in a predictor bank has the following contents (in this order):
    - 6 bit tag
    - 3-bit saturating counter for predictions
    - 2-bit *useful* saturating counter
    - 1 *allocated* bit
- An entry in a base predictor table is simply a 2-bit saturating counter
- The TAGE predictor also contains 24 circular shift registers to maintain an incremental folded hash
    - 16 6-bit CSRs for the index and the primary tag hash
    - 8 5-bit CSRs for the secondary tag hash
- A 4-bit saturating counter to toggle *alternate_on_new* (more on this in the [Predictions](#predictions) section below)
- a 164-bit register to hold the global history

### Predictions
- Simultaneously compute an index and hash for all predictor banks
- Look up the corresponding entry for each predictor bank
- The predictor bank with the longest history length with a matching tag provides the prediction, UNLESS
    - The criteria for *alternate_on_new* is met
    - If *alternate_on_new* is invoked, the next-highest predictor bank (designated *alt*) will instead be used
- For the selected bank, a counter with a value >= 4 means predict TAKEN; otherwise predict NOT TAKEN

*alternate_on_new* is an optimization heuristic where the *alt* history length (or the base predictor) is used instead of the longest under both of the following conditions:
- The counter of the highest history length is 3 or 4
- The useful counter is 0
- The 4-bit *alt_on_new* counter is 8 or higher

This is intended to prevent poorly-informed predictions from counters that haven't had much of a chance to train on branch history

Both the predictions made by the longest and alternate history lengths are recorded for the update step

### Updates
After a branch direction resolves, the following updates are made:
- The counter for the longest bank (or the base table if no match was found) is either incremented or decremented according to the result. 
- If the final prediction was incorrect and *alternate_on_new* was not invoked, then allocate a new entry
    - Loop over all provider banks for history lengths longer than the one used and look up the entry indexed by the current history and branch address
    - The first entry with a useful counter set to 0 is replaced. The 3-bit counter of the selected entry is set to either 4 or 3 if the branch resolved to TAKEN or NOT TAKEN, respectively, the useful counter is kept at 0, and the stored tag is overwritten by the new tag
    - NOTE: the original TAGE paper randomly selects a bank to allocate from. For simplicity, I chose to skip this.
    - If none of the allocation critera are met, decrement the *useful* counters of all predictor entries accessed
- If the alt prediction and actual prediction differ and *alternate_on_new* was not invoked, then the useful counter of the longest history is (in|de)cremented depending on whether the actual prediction was correct
- If *alternate_on_new* was invoked: if the prediction was correct, increment the counter; incorrent, decrement
- Update the circular shift registers with the new branch result

Additionally, all *useful* counters are reset to 0 every 256,000 branches.

### State validity
`predict()` and `update()` must happen in tandem for every branch encountered. Only states between consecutive `predict()`/`update()` pairs are considered valid; all other intermediate states are not well-defined.