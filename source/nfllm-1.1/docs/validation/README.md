# NFLLM 1.1 validation evidence

- `dos32a-bind/all/` records the six DOS executable bindings; each `SVER` log identifies DOS/32A 26.1.2 and each binder log names `DOS32A.EXE` as the embedded stub.
- `dos32a-bind/isa/` records the separately bound ISA probe.
- `nfdos-v23-dos32a-smoke.json` and its guest transcript record NFTEST passing and the real one-token TinyStories inference (token 419, `s`) in NF-DOS 6 V23, QEMU generic 486, 96 MiB.
- The boot image and model are not included. Their hashes are retained in the JSON; absolute local host paths were removed.
- This is a QEMU generic 486 check, not the exact 86Box Am486DX4/100 MHz retest.
