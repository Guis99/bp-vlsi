# Hardware simulation of a basic TAGE Branch Predictor
RTL simulation + transistor-level schematic + layout of a TAGE branch predictor

This project is a continuation of my exploration of branch predictors documented [here](https://bhhoh.com/blog/branch-prediction/). The goal is to better understand the details of computer hardware design by turning a toy C++ model into a slightly more realistic hardware model.

## Baseline spec
The TAGE predictor will fo

Since shrinking the TAGE Predictor will almost certainly degrade its performance on real traces, a Bimodal and GShare predictor will be used as a comparison point. All three predictors are sized to be no more than 1KiB (2^10 bytes). The TAGE predictor is sized as follows:

| Parameter | Value |
| :--- | :---: |
| Number of history lengths | 8 |
| Shortest history length | 4 |
| Longest history length | 164 |
| Entries per provider bank | 64 |
| Tag width | 6 |
| Base table entries | 512 |

This results in a total size of .93 KiB, or 7604 bits. The Bimodal and Gshare predictors are sized to be 8192 and 8202 bits, respectively.

