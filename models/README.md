# Model files

NFLLM does not distribute model weights. Download a compatible GGUF file from
its original source, check that source's license, and place the file on a DOS
volume that the program can read.

The Tiny1M F32 fixture used in the documented tests is available from the
[shibatch/tiny1m model card](https://huggingface.co/shibatch/tiny1m). The
recorded test file was 3,754,048 bytes with SHA-256
`60FA2675B0531735C9724C9FCB871BAA6E40594F451D1454981034BE47D025D4`. The
TinyStories 260K fixture was also used in testing; its redistribution terms
were not established, so it is not redistributed here.

Example, after obtaining a compatible file and transferring it to the DOS
volume:

```dos
NFLLM.EXE C:\TINY1M.GGF -p "Tom and Jerry are " -n 1
```

Model licenses are separate from the NFLLM source license. MIT licensing of
NFLLM does not grant rights to any model.
