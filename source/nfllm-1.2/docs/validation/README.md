# Validation records

## NFLLM 1.2

- `plugin-sdk-1.2.json` records the Plugin SDK build, lifecycle checks and
  DOSBox-X validation claims. The model-output details are in
  `graph-inference-1.2-vww0.json` and `graph-inference-1.2-vww1.json`.
- The NFD sizes and SHA-256 values in the plugin report identify the clean
  Open Watcom validation-build outputs. The three graph NFDs in the DOS release
  ZIP are each eight bytes larger and have different hashes. Their distributed
  hashes are recorded in `plugin-sdk-1.2.json`; the report does not establish
  byte-for-byte identity between those release files and the hashed validation
  build outputs. The cause of the difference has not been confirmed.
- The tests were reported using DOSBox-X's functional 486 profile, 96 MiB and
  FreeDOS 1.4. This does not represent cycle-accurate AMD Am486DX4 emulation.
- Model files, input frames, boot images and execution artifacts are not
  included in this release.

## NFLLM 1.1 historical evidence

- `nfdos-v23-dos32a-smoke.json` and
  `nfdos-v23-dos32a-guest-transcript.txt` record the earlier NF-DOS 6 V23
  smoke check and one-token TinyStories inference on a QEMU generic 486 profile.
- The boot image and model are not included. Their hashes are retained in the
  JSON; absolute local host paths were removed.
